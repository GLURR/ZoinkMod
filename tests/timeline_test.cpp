#include "../src/Timeline.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace wma;
void stepTo(Assist& a, Frame target) {
    while (a.frame() < target) a.nextStep();
}
int main() {
    Reference ref {240.0, {{500,true,true},{520,false,true},{530,true,true},{500,true,false},{510,false,false}}};
    {
        Assist a(ref);
        stepTo(a, 479);
        assert(a.onPhysicalInput(true,true,20)); // 20 frames early
        assert(a.onPhysicalInput(true,false,20));
        assert(a.pending() == 2);
        stepTo(a,499);
        assert(a.pending() == 2);
        auto out=a.nextStep();
        assert(out.size() == 2 && out[0].frame == 500 && out[1].frame == 500);
        assert(a.missed() == 0);
    }
    {
        Assist a(ref);
        stepTo(a,478);
        assert(!a.onPhysicalInput(true,true,20)); // 21 early
        stepTo(a, 490);
        assert(a.onPhysicalInput(true,true,20));
        stepTo(a, 500);
        assert(!a.onPhysicalInput(true,true,20)); // duplicate, next is RELEASE
        stepTo(a, 502);
        assert(a.onPhysicalInput(false,true,20)); // release buffered to 520
        stepTo(a, 519);
        auto out=a.nextStep();
        assert(out.size() == 1 && !out[0].pressed && out[0].frame == 520);
    }
    {
        Assist a(ref);
        stepTo(a, 501);
        assert(a.pending() == 0); // no physical press means NO autoplay
        assert(a.missed() >= 2);
    }
    {
        Assist a(ref);
        stepTo(a, 498);
        assert(!a.onPhysicalInput(false,true,20)); // polarity must match
        assert(a.onPhysicalInput(true,true,20));
        assert(a.nextStep().size() == 0);
        assert(a.nextStep().size() == 1);
        a.reset();
        assert(a.frame() == 0 && a.pending() == 0);
    }
    {
        Recorder recorder;
        recorder.add(3,true,true);
        recorder.add(3,true,true);
        recorder.add(7,false,true);
        recorder.add(9,true,false);
        assert(recorder.events().size() == 3);
        recorder.reset();
        assert(recorder.events().empty());
    }
    {
        Assist a(Reference{240.0, {{1,true,true},{2,false,true}}});
        assert(a.onPhysicalInput(true,true,0));
        assert(a.nextStep().size() == 1);
        assert(a.onPhysicalInput(false,true,0));
        assert(a.nextStep().size() == 1);
    }
    std::cout << "OK: wave timeline tests passed\n";
}
