#include <utils/ShaderPreprocessor.h>
#include <simplecpp.h>

#include <sstream>
#include <stdexcept>
#include <vector>

namespace t850 {
bool PreprocessShader(const std::string& source, const std::string& name,
                      const std::string& defines, std::string& output, std::string& diagnostic) {
  output.clear();
  diagnostic.clear();
  try {
    std::vector<std::string> filenames;
    simplecpp::OutputList messages;
    simplecpp::DUI options;
    options.std = "c++17";
    options.removeComments = true;
    std::istringstream input(defines + "\n" + source);
    simplecpp::TokenList tokens(input, filenames, name, options, &messages);
    std::vector<bool> alternatives;
    for (const auto* token = tokens.cfront(); token; token = token->next) {
      if (token->str() == "__DATE__" || token->str() == "__TIME__" || token->str() == "__FILE__")
        throw std::runtime_error("Nondeterministic predefined macros are not allowed");
      if (token->op != '#' || (token->previous && token->previous->location.sameline(token->location))) continue;
      const auto* directive = token->nextSkipComments();
      if (!directive || !directive->location.sameline(token->location)) continue;
      const auto& word = directive->str();
      if (word == "if" || word == "ifdef" || word == "ifndef") alternatives.push_back(false);
      else if (word == "endif") {
        if (alternatives.empty()) throw std::runtime_error("Unmatched #endif");
        alternatives.pop_back();
      } else if (word == "else" || word == "elif") {
        if (alternatives.empty() || alternatives.back()) throw std::runtime_error("Unexpected #else or #elif");
        if (word == "else") alternatives.back() = true;
      } else if (word != "define" && word != "undef" && word != "error") {
        throw std::runtime_error("Unsupported shader directive: #" + word);
      }
    }
    if (!alternatives.empty()) throw std::runtime_error("Unterminated conditional directive");
    simplecpp::FileDataCache includes;
    simplecpp::TokenList processed(filenames);
    if (messages.empty()) simplecpp::preprocess(processed, tokens, filenames, includes, options, &messages);
    if (!messages.empty()) {
      std::ostringstream details;
      for (const auto& message : messages) details << name << ':' << message.location.line << ": " << message.msg << '\n';
      diagnostic = details.str();
      return false;
    }
    std::ostringstream text;
    for (const auto* token = processed.cfront(); token; token = token->next) {
      if (token->comment) continue;
      text << token->str();
      text << (token->next && token->location.sameline(token->next->location) ? ' ' : '\n');
    }
    output = text.str();
    return true;
  } catch (const std::exception& error) {
    diagnostic = name + ": " + error.what();
    return false;
  }
}
}