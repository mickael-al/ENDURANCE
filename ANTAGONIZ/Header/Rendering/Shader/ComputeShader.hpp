#ifndef __ENGINE_COMPUTE_SHADER__
#define __ENGINE_COMPUTE_SHADER__

#include <string>

namespace Ge
{
	class ComputeShader
	{
	public:
		ComputeShader(std::string filename, std::string define = "", std::string traceLabel = "");
		~ComputeShader();
		ComputeShader(const ComputeShader&) = delete;
		ComputeShader& operator=(const ComputeShader&) = delete;
		void use();
		void dispatch(int num_groups_x, int num_groups_y, int num_groups_z);
		unsigned int getProgram() const;
		bool isValid() const noexcept { return m_computeProgram != 0; }
		const std::string& getLastError() const noexcept { return m_error; }
		// Transfer ownership to a caller that will eventually glDeleteProgram.
		unsigned int takeProgram() noexcept;
	private:
		unsigned int m_computeProgram = 0;
		std::string m_error;
	};
}

#endif //!__ENGINE_COMPUTE_SHADER__
