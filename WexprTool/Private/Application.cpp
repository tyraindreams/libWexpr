//
/// \file WexprTool/Application.cpp
/// \brief Tool to 
//
// #LICENSE_BEGIN:MIT#
// 
// Copyright (c) 2017-2018, Kenneth Perry (thothonegan)
// 
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
// 
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// 
// #LICENSE_END#
//

#include "CommandLineParser.hpp"

#include <libWexpr/libWexpr.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

namespace
{
	std::string s_readAllInputFrom (const std::string& inputPath)
	{
		std::string data;
		
		if (inputPath == "-")
		{
			return std::string (
				std::istreambuf_iterator<char>(std::cin), {}
			);
		}
		else
		{
			std::fstream file (inputPath, std::ios::in);
			
			return std::string (
				std::istreambuf_iterator<char>(file), {}
			);
		}
		
	}

	void s_writeAllOutputTo (const std::string& outputPath, const std::string& str)
	{
		if (outputPath == "-")
		{
			std::cout << str << std::flush;
		}
		else
		{
			std::fstream file (outputPath, std::ios::out | std::ios::trunc);
			
			file << str << std::flush;
		}
	}
	
	void s_writeAllOutputWithFileHeaderTo (const std::string& outputPath, void* buffer, size_t bufferSize)
	{
		std::fstream* f = nullptr;
		std::ostream* stream = &(std::cout);
		
		if (outputPath != "-")
		{
			f = new std::fstream(outputPath, std::ios::out | std::ios::binary);
			stream = f;
		}
		
		std::ostream& s = *stream; // the stream to write to
		
		// TODO: Move writing header to libWexpr since its part of the file format.
		
		// write header
		uint8_t header [20];
		memset (header, '\0', sizeof(header));
		header[0] = 0x83;
		header[1] = 'B'; header[2] = 'W'; header [3] = 'E' ; header[4] = 'X' ; header[5] = 'P'; header[6] = 'R';
		header[7] = 0x0A;
		
		*reinterpret_cast<uint32_t*>(header+8) = wexpr_uint32ToBig(0x00000001);
		
		// reserved is 0
		
		s.write (reinterpret_cast<const char*>(header), sizeof(header));
		
		// currently we have no aux chunks
		
		// write main chunk
		s.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(bufferSize));
		
		// flush to the stream
		s.flush();
		
		if (f)
		{
			delete f;
		}
	}
}

//
/// \brief App entry point
//
int main (int argc, char** argv)
{
	auto results = CommandLineParser::parse (argc, argv);
	
	if (results.version)
	{
		std::cout << "WexprTool " << wexpr_Version_major() << "." << wexpr_Version_minor() << "." << wexpr_Version_patch() << std::endl;
		
		return EXIT_SUCCESS;
	}
	
	if (results.help)
	{
		CommandLineParser::displayHelp(argc, argv);
		return EXIT_SUCCESS;
	}
	
	// normal flow
	if (results.command == CommandLineParser::Command::HumanReadable ||
		results.command == CommandLineParser::Command::Validate ||
		results.command == CommandLineParser::Command::Mini ||
		results.command == CommandLineParser::Command::Binary
	)
	{
		bool isValidate = (results.command == CommandLineParser::Command::Validate);
		
		auto inputStr = s_readAllInputFrom(results.inputPath);
		
		WexprError err = WEXPR_ERROR_INIT();
		
		// determine if binary or not.
		// if so, strip the header and do the chunk.
		WexprExpression* expr = nullptr;
		
		do { // so we can break back to here
		
			if (inputStr.size() >= 1 && static_cast<unsigned char>(inputStr[0]) == 0x83)
			{
				if (inputStr.size() < 20)
				{
					err.code = WexprErrorCodeBinaryInvalidHeader;
					err.column = 0;
					err.line = 0;
					err.message = strdup("Invalid binary header - not big enough");
					break;
				}
				
				uint8_t magic [8] = {
					0x83, 'B', 'W', 'E', 'X', 'P', 'R', 0x0A
				};
				
				if (memcmp(inputStr.data(), magic, sizeof(magic)) != 0)
				{
					err.code = WexprErrorCodeBinaryInvalidHeader;
					err.column = 0;
					err.line = 0;
					err.message = strdup("Invalid binary header - invalid magic");
					break;
				}
				
				if (*reinterpret_cast<const uint32_t*>(inputStr.data() + 8) != wexpr_uint32ToBig(0x01))
				{
					err.code = WexprErrorCodeBinaryUnknownVersion;
					err.column = 0;
					err.line = 0;
					err.message = strdup("Invalid binary header - unknown version");
					break;
				}
				
				// make sure reserved is blank
				uint8_t reserved [8] = {};
				if (memcmp(inputStr.data() + 12, reserved, 8) != 0)
				{
					err.code = WexprErrorCodeBinaryInvalidHeader;
					err.column = 0;
					err.line = 0;
					err.message = strdup("Invalid binary header - unknown reserved bits");
					break;
				}
				
				// header seems valid, skip it
				const uint8_t* data = reinterpret_cast<const uint8_t*> (inputStr.data());
				size_t curPos = 20;
				size_t endPos = inputStr.size();
				
				while (curPos < endPos)
				{
					// read the size and type
					size_t size = wexpr_bigUInt32ToNative(
						*reinterpret_cast<const uint32_t*> (
							data + curPos
						)
					);
					uint8_t type = data[curPos + sizeof(uint32_t)];
					if (/*given: type >= 0x00 &&*/ type <= 0x04)
					{
						// cool, parse it
						if (expr)
						{
							err.code = WexprErrorCodeBinaryMultipleExpressions;
							err.column = 0;
							err.line = 0;
							err.message = strdup("Found multiple expression chunks");
							break;
						}
						
						expr = wexpr_Expression_createFromBinaryChunk(
							data + curPos, size + sizeof(uint32_t) + sizeof(uint8_t),
							&err
						);
					}
					
					// move forward
					curPos += sizeof(uint32_t) + sizeof(uint8_t);
					curPos += size;
				}
			}
			else
			{
				// assume string
				expr = wexpr_Expression_createFromLengthString (
					inputStr.c_str(), inputStr.size(),
					WexprParseFlagNone,
					&err
				);
			}
		} while (0);
		
		if (err.code)
		{
			if (isValidate)
			{
				s_writeAllOutputTo(results.outputPath, "false\n");
				return EXIT_FAILURE;
			}
			else
			{
				std::string input = results.inputPath;
				if (input == "-")
					input = "(stdin)";
				
				std::cerr << "WexprTool: Error occurred with wexpr:" << std::endl;
				std::cerr << "WexprTool: " << input << ":" << err.line << ":" << err.column << ": " << err.message << std::endl;
				return EXIT_FAILURE;
			}
		}
		
		if (!expr)
		{
			if (isValidate)
			{
				s_writeAllOutputTo(results.outputPath, "false\n");
			}
			else
			{
				std::cerr << "WexprTool: Got an empty expression back" << std::endl;
			}
			
			return EXIT_FAILURE;
		}
		
		WEXPR_ERROR_FREE (err);
		
		if (isValidate)
		{
			s_writeAllOutputTo(results.outputPath, "true\n");
		}
		
		else if (results.command == CommandLineParser::Command::HumanReadable)
		{
			char* buffer = wexpr_Expression_createStringRepresentation (
				expr, 0, WexprWriteFlagHumanReadable
			);
				
				s_writeAllOutputTo(results.outputPath, std::string(buffer));
			free (buffer);
		}
		
		else if (results.command == CommandLineParser::Command::Mini)
		{
			char* buffer = wexpr_Expression_createStringRepresentation (
				expr, 0, WexprWriteFlagNone
			);
				
				s_writeAllOutputTo(results.outputPath, std::string(buffer));
			free (buffer);
		}
		
		else if (results.command == CommandLineParser::Command::Binary)
		{
			WexprMutableBuffer binDataInfo = wexpr_Expression_createBinaryRepresentation (
				expr
			);
			
			s_writeAllOutputWithFileHeaderTo(results.outputPath, binDataInfo.data, binDataInfo.byteSize);
			
			free (binDataInfo.data);
		}

		wexpr_Expression_destroy (expr);
	}
	
	else
	{
		std::cerr << "WexprTool: Unknown command" << std::endl;
		return EXIT_FAILURE;
	}
	
	return EXIT_SUCCESS;
}

