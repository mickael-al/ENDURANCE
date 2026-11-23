#pragma once

namespace Ge
{
// The decoder runs unchanged; only engine logging is omitted from this test.
struct Debug
{
    template<class... Args> static void Error(Args&&...) {}
    template<class... Args> static void Warn(Args&&...) {}
};
}
