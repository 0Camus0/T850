#include <pch.h>
#include <video/gl/GLSLParser.h>
#include <utils/Log.h>

#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iterator>
#include <fstream>
#include <set>


GLSL_Parser::GLSL_Parser(){
}

GLSL_Parser::~GLSL_Parser(){
}

void GLSL_Parser::ParseFromFile(std::string VSPath_, std::string FSPath_) {
{
  std::ifstream ifs(VSPath_);
  if (ifs.good()) {
    std::string str((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    buffer_vertex = str;
  }
  ifs.close();
}

{
  std::ifstream ifs(FSPath_);
  if (ifs.good()) {
    std::string str((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    buffer_fragment = str;
  }
  ifs.close();
}

	current_stage = hyperspace::shader::stage_::VERTEX_SHADER;
	Process(buffer_vertex);
	current_stage = hyperspace::shader::stage_::PIXEL_SHADER;
	Process(buffer_fragment);
}

void GLSL_Parser::ParseFromMemory(std::string VSSrc_, std::string FSSrc_) {
  buffer_vertex = VSSrc_;
  buffer_fragment = FSSrc_;

  current_stage = hyperspace::shader::stage_::VERTEX_SHADER;
  Process(buffer_vertex);
  current_stage = hyperspace::shader::stage_::PIXEL_SHADER;
  Process(buffer_fragment);
}

namespace {
	struct PreprocessorFrame {
		bool parentActive;
		bool active;
		bool branchTaken;
	};

	static std::string Trim(std::string value) {
		std::size_t first = value.find_first_not_of(" \t\r\n");
		if (first == std::string::npos)
			return std::string();
		std::size_t last = value.find_last_not_of(" \t\r\n");
		return value.substr(first, last - first + 1);
	}

	class DefineExpressionParser {
	public:
		DefineExpressionParser(const std::string& expression, const std::set<std::string>& definedSymbols)
			: m_expression(expression), m_definedSymbols(definedSymbols) {}

		bool Parse() {
			return ParseOr();
		}

	private:
		void SkipWhitespace() {
			while (m_pos < m_expression.size() && std::isspace(static_cast<unsigned char>(m_expression[m_pos])))
				++m_pos;
		}

		bool Match(const char* token) {
			SkipWhitespace();
			std::size_t length = std::strlen(token);
			if (m_expression.compare(m_pos, length, token) == 0) {
				m_pos += length;
				return true;
			}
			return false;
		}

		std::string ParseIdentifier() {
			SkipWhitespace();
			std::size_t start = m_pos;
			while (m_pos < m_expression.size()) {
				unsigned char ch = static_cast<unsigned char>(m_expression[m_pos]);
				if (!std::isalnum(ch) && ch != '_')
					break;
				++m_pos;
			}
			return m_expression.substr(start, m_pos - start);
		}

		bool ParsePrimary() {
			SkipWhitespace();
			if (Match("(")) {
				bool value = ParseOr();
				Match(")");
				return value;
			}

			std::string identifier = ParseIdentifier();
			if (identifier.empty())
				return false;

			if (identifier == "defined") {
				SkipWhitespace();
				bool hasParen = Match("(");
				std::string symbol = ParseIdentifier();
				if (hasParen)
					Match(")");
				return m_definedSymbols.count(symbol) != 0;
			}

			if (identifier == "1" || identifier == "true")
				return true;
			if (identifier == "0" || identifier == "false")
				return false;

			return m_definedSymbols.count(identifier) != 0;
		}

		bool ParseUnary() {
			if (Match("!"))
				return !ParseUnary();
			return ParsePrimary();
		}

		bool ParseAnd() {
			bool value = ParseUnary();
			while (Match("&&"))
				value = ParseUnary() && value;
			return value;
		}

		bool ParseOr() {
			bool value = ParseAnd();
			while (Match("||"))
				value = ParseAnd() || value;
			return value;
		}

		const std::string& m_expression;
		const std::set<std::string>& m_definedSymbols;
		std::size_t m_pos = 0;
	};

	static bool EvaluatePreprocessorExpression(const std::string& expression, const std::set<std::string>& definedSymbols) {
		DefineExpressionParser parser(expression, definedSymbols);
		return parser.Parse();
	}
}

// Simple #if/#ifdef/#ifndef/#elif/#else/#endif preprocessor so the parser only
// sees declarations from active code blocks. Handles the defined() expressions
// emitted by ShaderKey defines and preserves line count for diagnostics.
static std::string PreprocessIfdefs(const std::string& src) {
  std::istringstream iss(src);
  std::string line, result;
  std::set<std::string> defines;
	std::vector<PreprocessorFrame> stack;
	stack.push_back({true, true, false});

  while (std::getline(iss, line)) {
		std::string trimmed = Trim(line);

		if (trimmed.rfind("#define ", 0) == 0 && stack.back().active) {
			std::string sym = Trim(trimmed.substr(8));
			auto sp = sym.find_first_of(" \t");
      if (sp != std::string::npos) sym = sym.substr(0, sp);
      defines.insert(sym);
      result += '\n'; // don't emit the #define line
    } else if (trimmed.rfind("#ifdef ", 0) == 0) {
			std::string sym = Trim(trimmed.substr(7));
			bool condition = defines.count(sym) != 0;
			bool parentActive = stack.back().active;
			stack.push_back({parentActive, parentActive && condition, condition});
      result += '\n';
    } else if (trimmed.rfind("#ifndef ", 0) == 0) {
			std::string sym = Trim(trimmed.substr(8));
			bool condition = defines.count(sym) == 0;
			bool parentActive = stack.back().active;
			stack.push_back({parentActive, parentActive && condition, condition});
			result += '\n';
		} else if (trimmed.rfind("#if ", 0) == 0) {
			bool condition = EvaluatePreprocessorExpression(trimmed.substr(4), defines);
			bool parentActive = stack.back().active;
			stack.push_back({parentActive, parentActive && condition, condition});
			result += '\n';
		} else if (trimmed.rfind("#elif", 0) == 0) {
			if (stack.size() > 1) {
				PreprocessorFrame& frame = stack.back();
				bool condition = !frame.branchTaken && EvaluatePreprocessorExpression(trimmed.substr(5), defines);
				frame.active = frame.parentActive && condition;
				frame.branchTaken = frame.branchTaken || condition;
			}
      result += '\n';
    } else if (trimmed.rfind("#else", 0) == 0) {
      if (stack.size() > 1) {
				PreprocessorFrame& frame = stack.back();
				frame.active = frame.parentActive && !frame.branchTaken;
				frame.branchTaken = true;
      }
      result += '\n';
    } else if (trimmed.rfind("#endif", 0) == 0) {
      if (stack.size() > 1) stack.pop_back();
      result += '\n';
    } else {
			if (stack.back().active)
        result += line + '\n';
      else
        result += '\n'; // keep line count stable
    }
  }
  return result;
}

static std::string StripComments(const std::string& src) {
	std::string result;
	result.reserve(src.size());

	bool inLineComment = false;
	bool inBlockComment = false;

	for (std::size_t i = 0; i < src.size(); ++i) {
		char ch = src[i];
		char next = (i + 1 < src.size()) ? src[i + 1] : '\0';

		if (inLineComment) {
			if (ch == '\n') {
				inLineComment = false;
				result += ch;
			}
			continue;
		}

		if (inBlockComment) {
			if (ch == '\n') result += ch;
			if (ch == '*' && next == '/') {
				inBlockComment = false;
				++i;
			}
			continue;
		}

		if (ch == '/' && next == '/') {
			inLineComment = true;
			++i;
			continue;
		}

		if (ch == '/' && next == '*') {
			inBlockComment = true;
			++i;
			continue;
		}

		result += ch;
	}

	return result;
}

void GLSL_Parser::Process(std::string &b) {

	std::string preprocessed = StripComments(PreprocessIfdefs(b));
	std::istringstream iss(preprocessed);
	std::vector<std::string> tokens{ std::istream_iterator<std::string>{iss},std::istream_iterator<std::string>{} };

	// Debug: log preprocessed tokens for shaders with 'in' keyword
	static bool sLoggedOnce = false;
	if (!sLoggedOnce && current_stage == hyperspace::shader::stage_::VERTEX_SHADER) {
		for (std::size_t i = 0; i < tokens.size() && i < 30; i++) {
			T8_LOG_TRACE("[GLSL_Parse] token[%zu]='%s'", i, tokens[i].c_str());
		}
		sLoggedOnce = true;
	}

	std::string::size_type pos = 0;
	for (std::size_t i = 0; i < tokens.size(); i++)	{

		if ((pos = tokens[i].find("uniform")) != std::string::npos) {
			ProcessToken(i, tokens);
		}

		if ((pos = tokens[i].find("varying")) != std::string::npos) {
			ProcessToken(i, tokens);
		}

		if ((pos = tokens[i].find("attribute")) != std::string::npos) {
			ProcessToken(i, tokens);
		}

		// GLSL 330 / ES 3.0: 'in' replaces 'attribute' (vertex) and
		// 'varying' (fragment). Only match the exact token "in" to
		// avoid false positives on words like "int", "input", etc.
		// Only process in vertex shader stage — fragment 'in' varyings
		// are not needed for vertex attribute layout computation.
		if (tokens[i] == "in" && current_stage == hyperspace::shader::stage_::VERTEX_SHADER) {
			ProcessToken(i, tokens);
		}
	}
}

void GLSL_Parser::ProcessToken(std::size_t &pos, std::vector<std::string> &v) {
	std::size_t token_pos = pos;
	for (;;) {
		if (v[token_pos].find(";") != std::string::npos) break;
		token_pos++;
	}

	GLSL_Var_ var_;
	var_.name = v[token_pos].substr(0, v[token_pos].size() - 1);
	var_.stage = current_stage;
  DetermineArrayNum(var_, v[token_pos]);
	DetermineSemantic(var_, v[pos]);
	DetermineType(var_, v[token_pos - 1]);

	switch (var_.sem){
		case hyperspace::shader::semantic_::ATTRIBUTE: {
			attributes.push_back(var_);
		}break;
		case hyperspace::shader::semantic_::UNIFORM: {
			uniforms.push_back(var_);
		}break;
		case hyperspace::shader::semantic_::VARYING: {
			varying.push_back(var_);
		}break;
		case hyperspace::shader::semantic_::UNKNOWN_SEMANTIC: {
			/*varying.push_back(var_);*/
		}break;
	}

}

void GLSL_Parser::DetermineSemantic(GLSL_Var_ &var, std::string &str) {
	if (str.find("uniform") != std::string::npos) {
		var.sem = hyperspace::shader::semantic_::UNIFORM;
	}else if (str.find("varying") != std::string::npos) {
		var.sem = hyperspace::shader::semantic_::VARYING;
	}else if (str.find("attribute") != std::string::npos) {
		var.sem = hyperspace::shader::semantic_::ATTRIBUTE;
	}else if (str == "in") {
		// GLSL 330 / ES 3.0: 'in' = attribute in vertex, varying in fragment
		if (current_stage == hyperspace::shader::stage_::VERTEX_SHADER)
			var.sem = hyperspace::shader::semantic_::ATTRIBUTE;
		else
			var.sem = hyperspace::shader::semantic_::VARYING;
	}else if (str == "out") {
		// 'out' in vertex shader = varying output (not tracked here)
		var.sem = hyperspace::shader::semantic_::VARYING;
	}else {
		var.sem = hyperspace::shader::semantic_::UNKNOWN_SEMANTIC;
	}
}

void GLSL_Parser::DetermineType(GLSL_Var_ &var, std::string &str) {
	if (str.find("int") != std::string::npos) {
		var.type = hyperspace::shader::datatype_::INT_;
	}else if (str.find("float") != std::string::npos) {
		var.type = hyperspace::shader::datatype_::FLOAT_;
	}else if (str.find("bool") != std::string::npos) {
		var.type = hyperspace::shader::datatype_::BOOLEAN_;
	}else if (str.find("vec2") != std::string::npos) {
		var.type = hyperspace::shader::datatype_::VECTOR2_;
	}else if (str.find("vec3") != std::string::npos) {
		var.type = hyperspace::shader::datatype_::VECTOR3_;
	}else if (str.find("vec4") != std::string::npos) {
		var.type = hyperspace::shader::datatype_::VECTOR4_;
	}else if (str.find("mat2") != std::string::npos) {
		var.type = hyperspace::shader::datatype_::MAT2_;
	}else if (str.find("mat3") != std::string::npos) {
		var.type = hyperspace::shader::datatype_::MAT3_;
	}else if (str.find("mat4") != std::string::npos) {
		var.type = hyperspace::shader::datatype_::MAT4_;
	}else if (str.find("sampler1D") != std::string::npos) {
		var.type = hyperspace::shader::datatype_::SAMPLER1D_;
	}else if (str.find("sampler2D") != std::string::npos) {
		var.type = hyperspace::shader::datatype_::SAMPLER2D_;
	}else if (str.find("sampler3D") != std::string::npos) {
		var.type = hyperspace::shader::datatype_::SAMPLER3D_;
	}else if (str.find("samplerCube?") != std::string::npos) {
		var.type = hyperspace::shader::datatype_::SAMPLERCUBE_;
	}else if (str.find("sampler2DShadow?") != std::string::npos) {
		var.type = hyperspace::shader::datatype_::SAMPLERSHADOW_;
	}else {
		var.type = hyperspace::shader::datatype_::UNKNOWN_TYPE;
	}
}

void GLSL_Parser::DetermineArrayNum(GLSL_Var_ & var, std::string & str)
{
  auto arrPos = str.find("[");
  if (arrPos != std::string::npos) {
    auto arrFPos = str.find("]");
    std::string numStr = str.substr(arrPos+1,arrFPos);
    var.numItems = atoi(numStr.c_str());
    var.name = str.substr(0, arrPos);
  }
  else {
    var.numItems = 1;
  }
}
