#ifndef __PROCESS_GUARD__
#define __PROCESS_GUARD__

namespace ProcessGuard
{
	// Enabled only in the Windows player Release build. Editor and Debug builds
	// deliberately remain attachable.
	bool isProtectionEnabled();
	bool isDebuggerAttached();
	bool isForbiddenToolRunning();
	void enforceNoDebugger();
	void startMonitoring();
}

#endif //!__PROCESS_GUARD__
