#include "AtmosphereState.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
    Ge::AtmospherePalette palette(float value)
    {
        Ge::AtmospherePalette p;
        p.horizon=glm::vec3(value,value*0.5f,value*0.25f);
        p.middle=glm::vec3(value*0.5f,value*0.25f,value);
        p.zenith=glm::vec3(value*0.25f,value,value*0.5f);
        p.middleElevation=0.25f+value*0.1f;
        p.gradientPower=1.0f+value*0.2f;
        p.strength=value*0.3f;
        p.exposure=1.0f+value;
        return p;
    }

    void same(const Ge::AtmospherePalette& actual,const Ge::AtmospherePalette& expected,float tolerance=2e-6f)
    {
        const float difference=Ge::atmospherePaletteDifference(actual,expected);
        assert(std::isfinite(difference) && difference<=tolerance);
        assert(std::isfinite(actual.horizon.x) && std::isfinite(actual.exposure));
    }
}

int main()
{
    const auto dark=palette(0), bright=palette(1), hdr=palette(4);
    Ge::AtmosphereTransition t;
    assert(!t.initialized() && !t.active() && !t.advance(0.5));
    assert(t.target(dark,0.01f,1));
    assert(t.initialized() && !t.active());
    same(t.displayed(),dark);
    assert(t.target(bright,0.01f,1));
    same(t.displayed(),dark);
    assert(t.advance(0.5));
    same(t.displayed(),Ge::mixAtmospherePalette(dark,bright,0.5f));

    // A new destination starts at the currently displayed value. Merely
    // accepting it must not snap or restart from the former dark endpoint.
    const auto interrupted=t.displayed();
    assert(t.target(hdr,0.01f,1));
    same(t.displayed(),interrupted);
    assert(t.advance(0.5));
    same(t.displayed(),Ge::mixAtmospherePalette(interrupted,hdr,0.5f));
    assert(t.advance(0.5));
    same(t.displayed(),hdr);
    assert(!t.active());
    assert(!t.advance(1));
    std::cout << "PASS retargeting: continuous interruption, HDR endpoints, completion with no camera events\n";

    // Each rejected sample is close to its predecessor, but the fifth sample
    // differs enough from the last accepted destination. Rejecting a sample
    // must not move the reference used by the threshold.
    t.reset();
    assert(t.target(dark,0.1f,1));
    for(float increment:{0.02f,0.04f,0.06f,0.08f})
    {
        assert(!t.target(palette(increment),0.1f,1));
        same(t.accepted(),dark);
    }
    assert(t.target(palette(0.12f),0.1f,1));
    same(t.accepted(),palette(0.12f));
    assert(t.advance(1));
    same(t.displayed(),palette(0.12f));
    assert(t.target(bright,10.0f,0.0f,true));
    same(t.displayed(),bright);
    assert(!t.active());
    std::cout << "PASS thresholds: sub-threshold movement accumulates; forced seed/parameter update bypasses threshold\n";

    // A fixed endpoint and elapsed time must give the same colour under very
    // different frame partitions, including a hitch exceeding the duration.
    Ge::AtmosphereTransition one,sixty,irregular;
    for(auto* state:{&one,&sixty,&irregular})
    {
        state->target(dark,0.01f,1);
        state->target(hdr,0.01f,1);
    }
    one.advance(0.75);
    for(int frame=0;frame<45;++frame) sixty.advance(1.0/60.0);
    for(double dt:{0.03,0.07,0.2,0.005,0.445}) irregular.advance(dt);
    same(one.displayed(),sixty.displayed());
    same(one.displayed(),irregular.displayed());
    const auto beforeInvalid=one.displayed();
    for(double dt:{0.0,-1.0,std::numeric_limits<double>::infinity(),
                   -std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
    {
        assert(!one.advance(dt));
        same(one.displayed(),beforeInvalid);
    }
    one.advance(10);
    same(one.displayed(),hdr);
    assert(!one.active());
    std::cout << "PASS time: frame-rate independence, stationary continuation, invalid dt rejection and hitch clamp\n";

    // Parameter-only edits need to invalidate the sky just as colour edits do.
    for(int parameter=0;parameter<4;++parameter)
    {
        auto changed=dark;
        if(parameter==0) changed.middleElevation+=0.2f;
        if(parameter==1) changed.gradientPower+=0.2f;
        if(parameter==2) changed.strength+=0.2f;
        if(parameter==3) changed.exposure+=0.2f;
        t.reset(); t.target(dark,0.1f,1);
        assert(t.target(changed,0.1f,1));
        t.advance(1);
        same(t.displayed(),changed);
    }
    std::cout << "PASS gradient controls: all four non-colour controls participate in invalidation and interpolation\n";
}
