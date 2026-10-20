#pragma once
// This module is intentionally Geode-free so it can be unit tested locally.
#include <array>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace wma {
using Frame = std::uint64_t;
struct Input {
    Frame frame = 0;
    bool pressed = false;
    bool player1 = true;
};
struct Reference {
    double physicsHz = 240.0;
    std::vector<Input> events;
};

class Recorder {
public:
    void reset() { m_events.clear(); m_down = {false,false}; }
    // Record only state transitions, not repeat press events while held.
    void add(Frame frame, bool pressed, bool p1) {
        auto p = p1 ? 0 : 1;
        if (frame == 0 || m_down[p] == pressed) return;
        m_down[p] = pressed;
        m_events.push_back({frame, pressed, p1});
    }
    std::vector<Input> const& events() const { return m_events; }
private:
    std::vector<Input> m_events;
    std::array<bool,2> m_down{false,false};
};

class Assist {
public:
    explicit Assist(Reference ref = {}) { setReference(std::move(ref)); }
    void setReference(Reference ref) {
        m_reference = std::move(ref);
        // Sort so multiple players with identical frame remain stable.
        std::stable_sort(m_reference.events.begin(), m_reference.events.end(),
            [](Input const& a, Input const& b) { return a.frame < b.frame; });
        for (auto& tr : m_track) tr.clear();
        for (auto const& event : m_reference.events)
            m_track[event.player1 ? 0 : 1].push_back(event);
        reset();
    }
    Reference const& reference() const { return m_reference; }
    void reset() {
        m_frame = 0;
        m_cursors = {0,0};
        m_pending.clear();
        m_accepted = m_missed = 0;
    }
    // Call on each real physics step, BEFORE the game's physics advances.
    std::vector<Input> nextStep() {
        ++m_frame;
        dropExpired(m_frame);
        std::vector<Input> due;
        auto it = m_pending.begin();
        while (it != m_pending.end()) {
            if (it->frame == m_frame) {
                due.push_back(*it);
                it = m_pending.erase(it);
            } else if (it->frame < m_frame) {
                it = m_pending.erase(it); // defensive, never play late
            } else ++it;
        }
        return due;
    }
    // An input issued after frame X's physics applies no earlier than X+1.
    // Input must be IN the window [target-window, target]; wrong polarity never queues.
    bool onPhysicalInput(bool pressed, bool player1, int window) {
        auto p = player1 ? 0 : 1;
        Frame actual = m_frame + 1;
        dropExpired(actual);
        auto& cursor = m_cursors[p];
        auto const& tr = m_track[p];
        if (cursor >= tr.size()) return false;
        auto const& target = tr[cursor];
        if (target.pressed != pressed || target.frame < actual ||
            target.frame - actual > static_cast<Frame>(std::clamp(window, 0, 120))) return false;
        m_pending.push_back(target);
        ++cursor;
        ++m_accepted;
        return true;
    }
    Frame frame() const { return m_frame; }
    std::size_t pending() const { return m_pending.size(); }
    std::size_t accepted() const { return m_accepted; }
    std::size_t missed() const { return m_missed; }
    bool empty() const { return m_reference.events.empty(); }
private:
    void dropExpired(Frame frame) {
        for (int p=0; p<2; ++p) {
            auto& cursor = m_cursors[p];
            auto const& tr = m_track[p];
            while (cursor < tr.size() && tr[cursor].frame < frame) {
                ++cursor;
                ++m_missed;
            }
        }
    }
    Reference m_reference;
    std::array<std::vector<Input>,2> m_track;
    std::array<std::size_t,2> m_cursors{0,0};
    std::vector<Input> m_pending;
    Frame m_frame = 0;
    std::size_t m_accepted = 0;
    std::size_t m_missed = 0;
};
} // namespace wma
