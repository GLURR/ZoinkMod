#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "Timeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

using namespace geode::prelude;

namespace {

enum class Mode { Off, Record, Assist, Incompatible };
struct Settings {
    bool enabled = true;
    bool recordNext = false;
    int earlyFrames = 20;
    bool player1 = true;
    bool player2 = true;
    bool overlay = true;
};
Settings s_settings;
Mode s_mode = Mode::Off;
PlayLayer* s_layer = nullptr;
wma::Recorder s_recorder;
wma::Assist s_assist;
wma::Frame s_frame = 0;
double s_physicsHz = 0.0;
std::array<bool,2> s_forwardedHeld{false,false};
std::filesystem::path s_referenceFile;
std::string s_status;
std::size_t s_dispatched = 0;

void readSettings() {
    auto* mod = Mod::get();
    s_settings.enabled = mod->getSettingValue<bool>("enabled");
    s_settings.recordNext = mod->getSettingValue<bool>("record-next");
    auto early = mod->getSettingValue<std::int64_t>("early-frames");
    s_settings.earlyFrames = static_cast<int>(std::clamp<std::int64_t>(early,0,120));
    s_settings.player1 = mod->getSettingValue<bool>("p1");
    s_settings.player2 = mod->getSettingValue<bool>("p2");
    s_settings.overlay = mod->getSettingValue<bool>("overlay");
}
bool activeLayer(GJBaseGameLayer* layer) {
    return s_layer && s_layer == layer && PlayLayer::get() == s_layer &&
           !s_layer->m_playerDied && !s_layer->m_hasCompletedLevel;
}
bool selected(bool p1) { return p1 ? s_settings.player1 : s_settings.player2; }
bool inWave(GJBaseGameLayer* layer, bool p1) {
    auto* player = p1 ? layer->m_player1 : layer->m_player2;
    return player && player->m_isDart && !player->m_isDead;
}
std::uint64_t fnv1a(std::string_view data) {
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char ch : data) { h ^= ch; h *= 1099511628211ull; }
    return h;
}
std::filesystem::path pathFor(GJGameLevel* level) {
    auto id = level ? level->m_levelID.value() : 0;
    std::string_view data;
    if (level && !level->m_levelString.empty())
        data = {level->m_levelString.c_str(), level->m_levelString.size()};
    else if (level)
        data = {level->m_levelName.c_str(), level->m_levelName.size()};
    auto hash = fnv1a(data);
    // File key distinguishes downloaded levels, edited versions and most local levels.
    return Mod::get()->getSaveDir() / "references" /
           fmt::format("level-{}-{:016x}.wma", id, hash);
}

bool saveReference(std::filesystem::path const& path, wma::Reference const& reference) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) return false;
        out << "WMA1 " << std::setprecision(12) << reference.physicsHz << ' ' << reference.events.size() << '\n';
        for (auto const& e : reference.events)
            out << e.frame << ' ' << (e.pressed ? 1 : 0) << ' ' << (e.player1 ? 1 : 0) << '\n';
        out.flush();
        if (!out.good()) return false;
    }
    // Keep an old reference as backup if the new file cannot be installed.
    auto backup = path;
    backup += ".bak";
    std::filesystem::remove(backup, ec);
    ec.clear();
    bool hadOld = std::filesystem::exists(path, ec) && !ec;
    if (hadOld) {
        std::filesystem::rename(path, backup, ec);
        if (ec) return false;
    }
    ec.clear();
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        if (hadOld) {
            std::error_code recoveryError;
            std::filesystem::rename(backup, path, recoveryError);
        }
        return false;
    }
    if (hadOld) std::filesystem::remove(backup, ec);
    return true;
}

bool loadReference(std::filesystem::path const& path, wma::Reference& ref) {
    std::ifstream in(path);
    if (!in) return false;
    std::string version;
    std::size_t count = 0;
    if (!(in >> version >> ref.physicsHz >> count) || version != "WMA1" ||
        !(ref.physicsHz >= 20.0 && ref.physicsHz <= 3000.0) || count > 300000 || count == 0) return false;
    ref.events.clear();
    ref.events.reserve(count);
    wma::Frame lastFrame = 0;
    for (std::size_t i=0; i<count; ++i) {
        wma::Frame frame; int pressed, p1;
        if (!(in >> frame >> pressed >> p1) || frame < lastFrame || frame == 0 ||
             pressed < 0 || pressed > 1 || p1 < 0 || p1 > 1) return false;
        ref.events.push_back({frame, static_cast<bool>(pressed), static_cast<bool>(p1)});
        lastFrame = frame;
    }
    return true;
}

void initializeFor(GJGameLevel* level) {
    readSettings();
    s_referenceFile = pathFor(level);
    s_frame = 0;
    s_physicsHz = 0.0;
    s_dispatched = 0;
    s_forwardedHeld = {false,false};
    s_recorder.reset();
    s_assist.reset();
    s_mode = Mode::Off;
    s_status = "NO REFERENCE - activate Record next run in Geode";
    if (!s_settings.enabled) { s_status = "WMA DISABLED"; return; }
    if (s_settings.recordNext) {
        s_mode = Mode::Record;
        s_status = "REC: complete the level to save your reference";
        return;
    }
    wma::Reference reference;
    if (loadReference(s_referenceFile, reference)) {
        s_assist.setReference(std::move(reference));
        s_mode = Mode::Assist;
        s_status = "ASSIST: reference auto-loaded";
    }
}

void resetAttempt() {
    s_frame = 0;
    s_physicsHz = 0.0;
    s_dispatched = 0;
    s_forwardedHeld = {false,false};
    s_recorder.reset();
    s_assist.reset();
}

void verifyPhysics(double dt) {
    if (!(dt > 0) || dt > 0.2) return;
    double hz = 1.0 / dt;
    if (!s_physicsHz) {
        s_physicsHz = hz;
        if (s_mode == Mode::Assist &&
            std::abs(hz - s_assist.reference().physicsHz) >
                std::max(1.0, s_assist.reference().physicsHz * .015)) {
            s_mode = Mode::Incompatible;
            s_status = "TPS MISMATCH - recording cannot be applied";
        }
    }
}

const char* modeLabel() {
    switch (s_mode) {
        case Mode::Record: return "REC";
        case Mode::Assist: return "ASSIST";
        case Mode::Incompatible: return "TPS MISMATCH";
        default: return "OFF";
    }
}
} // namespace

class $modify(WaveMemoryInputs, GJBaseGameLayer) {
    void handleButton(bool pressed, int button, bool player1) {
        if (!activeLayer(this) || button != 1 || !s_settings.enabled ||
            !selected(player1) || !inWave(this, player1)) {
            // Any gamemode other than wave behaves normally.
            if (activeLayer(this) && button == 1)
                s_forwardedHeld[player1 ? 0 : 1] = pressed;
            GJBaseGameLayer::handleButton(pressed, button, player1);
            return;
        }
        if (s_mode == Mode::Record) {
            s_recorder.add(s_frame + 1, pressed, player1);
            s_forwardedHeld[player1 ? 0 : 1] = pressed;
            GJBaseGameLayer::handleButton(pressed, button, player1);
            return;
        }
        if (s_mode == Mode::Assist) {
            // Capture only! This NEVER forwards a physical wave input immediately.
            // No macro event fires without a corresponding real press/release.
            s_assist.onPhysicalInput(pressed, player1, s_settings.earlyFrames);
            return;
        }
        s_forwardedHeld[player1 ? 0 : 1] = pressed;
        GJBaseGameLayer::handleButton(pressed, button, player1);
    }

    void processCommands(float dt, bool firstStep, bool secondStep) {
        if (activeLayer(this) && s_settings.enabled) {
            verifyPhysics(dt);
            if (s_mode == Mode::Assist) {
                auto actions = s_assist.nextStep();
                s_frame = s_assist.frame();
                for (auto const& action : actions) {
                    auto const p = action.player1 ? 0 : 1;
                    // Do not leak a buffered wave event into cube/ship/etc.
                    if (!selected(action.player1) || !inWave(this, action.player1)) continue;
                    if (s_forwardedHeld[p] == action.pressed) continue;
                    s_forwardedHeld[p] = action.pressed;
                    GJBaseGameLayer::handleButton(action.pressed, 1, action.player1);
                    ++s_dispatched;
                }
            } else {
                ++s_frame;
            }
        }
        GJBaseGameLayer::processCommands(dt, firstStep, secondStep);
    }
};

class $modify(WaveMemoryPlay, PlayLayer) {
    struct Fields {
        cocos2d::CCLabelBMFont* indicator = nullptr;
    };
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        s_layer = this;
        initializeFor(level);
        auto* label = cocos2d::CCLabelBMFont::create("", "bigFont.fnt");
        if (label) {
            label->setScale(.26f);
            label->setAnchorPoint({0,1});
            label->setPosition({8.f, cocos2d::CCDirector::sharedDirector()->getWinSize().height - 8.f});
            label->setOpacity(230);
            addChild(label, 100);
            m_fields->indicator = label;
        }
        return true;
    }
    void resetLevel() {
        PlayLayer::resetLevel();
        if (s_layer == this) {
            // The record toggle can also be changed from the Geode menu
            // while the level is paused; apply it on the next restart.
            bool wantRecord = Mod::get()->getSettingValue<bool>("record-next");
            if (wantRecord != (s_mode == Mode::Record)) initializeFor(m_level);
            else {
                readSettings();
                resetAttempt();
            }
        }
    }
    void levelComplete() {
        if (s_layer == this && s_mode == Mode::Record && !s_recorder.events().empty()) {
            wma::Reference ref;
            ref.physicsHz = s_physicsHz > 0.0 ? s_physicsHz : 240.0;
            ref.events = s_recorder.events();
            if (saveReference(s_referenceFile, ref)) {
                s_assist.setReference(ref);
                s_mode = Mode::Assist;
                Mod::get()->setSettingValue<bool>("record-next", false);
                s_status = fmt::format("SAVED: {} wave input events", ref.events.size());
            } else {
                s_status = "ERROR: couldn't write reference to disk";
            }
        }
        PlayLayer::levelComplete();
    }
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto* label = m_fields->indicator;
        if (!label) return;
        label->setVisible(s_settings.overlay && s_layer == this);
        if (!s_settings.overlay || s_layer != this) return;
        std::string text;
        if (s_mode == Mode::Record) {
            text = fmt::format("WMA REC | frame:{} | inputs:{}", s_frame, s_recorder.events().size());
        } else if (s_mode == Mode::Assist) {
            text = fmt::format("WMA ASSIST {}f | frame:{} | buffered:{} sent:{} missed:{}",
                s_settings.earlyFrames, s_frame, s_assist.pending(), s_dispatched, s_assist.missed());
        } else {
            text = fmt::format("WMA {} | {}", modeLabel(), s_status);
        }
        label->setString(text.c_str());
    }
    void onQuit() {
        if (s_layer == this) { s_layer = nullptr; s_mode = Mode::Off; }
        PlayLayer::onQuit();
    }
};
