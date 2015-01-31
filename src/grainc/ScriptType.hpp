#ifndef GRAINC_SCRIPT_TYPE_HPP
#define GRAINC_SCRIPT_TYPE_HPP

#include <string>

namespace ScriptType
{
	enum Enum
	{
		Affector,
		Emitter,
		VertexShader,
		FragmentShader
	};

	bool parse(const std::string& str, Enum& out);
}

#endif
