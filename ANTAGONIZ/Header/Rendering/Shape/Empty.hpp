#ifndef __EMPTY_OBJECT__
#define __EMPTY_OBJECT__

#include "GObject.hpp"
//#include "MemoryManager.hpp"

namespace Ge
{
	class Empty final : public GObject
	{
	public:		
		void mapMemory();
	};
}

#endif //!__EMPTY_OBJECT__