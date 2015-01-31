#ifndef GRAINC_LOGGER_HPP
#define GRAINC_LOGGER_HPP

#include "grainc.hpp"
#include <sstream>

class Logger
{
public:
	Logger(ILogStream* stream)
		:mLogStream(stream)
	{}

	~Logger()
	{
		mLogStream->write(mStringStream.str().c_str());
	}

	template<typename T>
	Logger& operator<<(const T& t)
	{
		mStringStream << t;
		return *this;
	}

private:
	ILogStream* mLogStream;
	std::stringstream mStringStream;
};

#endif
