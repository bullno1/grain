#include "ScriptType.hpp"

bool ScriptType::parse(const std::string& extension, ScriptType::Enum& out)
{
	if(extension == "emitter")
	{
		out = Emitter;
		return true;
	}
	else if(extension == "affector")
	{
		out = Affector;
		return true;
	}
	else if(extension == "vsh")
	{
		out = VertexShader;
		return true;
	}
	else if(extension == "fsh")
	{
		out = FragmentShader;
		return true;
	}
	else
	{
		return false;
	}
}
