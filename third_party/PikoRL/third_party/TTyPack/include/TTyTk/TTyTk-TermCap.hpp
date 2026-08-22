#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-EscapeCodes.hpp>
#include <TTyTk/TTyTk-TermInfo.hpp>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <unordered_map>

namespace ttytk::termcap {
struct Entry {
    std::string names{};
    std::unordered_map<std::string, bool> flags{};
    std::unordered_map<std::string, int> numbers{};
    std::unordered_map<std::string, std::string> strings{};
    [[nodiscard]] inline auto flag(const std::string_view name) const -> bool { auto it=flags.find(std::string(name)); return it!=flags.end() && it->second; }
    [[nodiscard]] inline auto num(const std::string_view name) const -> std::optional<int> { auto it=numbers.find(std::string(name)); return it==numbers.end()?std::nullopt:std::optional<int>(it->second); }
    [[nodiscard]] inline auto str(const std::string_view name) const -> std::optional<std::string_view> { auto it=strings.find(std::string(name)); return it==strings.end()?std::nullopt:std::optional<std::string_view>(it->second); }
};
namespace detail {
inline auto unescape(const std::string_view in) -> std::string { std::string out{}; for(std::size_t i=0;i<in.size();++i) { if(in[i]=='^'&&i+1<in.size()) out+=static_cast<char>(in[++i]&0x1f); else if(in[i]=='\\'&&i+1<in.size()) { const char c=in[++i]; if(c=='E'||c=='e')out+='\x1b'; else if(c=='n')out+='\n'; else if(c=='r')out+='\r'; else if(c=='t')out+='\t'; else out+=c; } else out+=in[i]; } return out; }
}
// Termcap.man: fields are colon-separated, with booleans, #numbers and =strings.
[[nodiscard]] inline auto load_from(const std::string_view text) -> Result<Entry> {
    const auto line_end=text.find('\n'); const auto line=text.substr(0,line_end);
    const auto first=line.find(':'); if(first==std::string_view::npos) return Error{EINVAL,"invalid termcap entry"};
    Entry entry{}; entry.names=std::string(line.substr(0,first));
    std::size_t begin=first+1; while(begin<line.size()) { const auto end=line.find(':',begin); const auto field=line.substr(begin,end-begin); if(field.size()>=2) { const std::string key(field.substr(0,2)); if(field.size()==2) entry.flags[key]=true; else if(field[2]=='#') { try {entry.numbers[key]=std::stoi(std::string(field.substr(3)));} catch(...) {return Error{EINVAL,"invalid termcap number"};} } else if(field[2]=='=') entry.strings[key]=detail::unescape(field.substr(3)); } if(end==std::string_view::npos)break; begin=end+1; }
    return entry;
}
[[nodiscard]] inline auto load(const std::string& name) -> Result<Entry> {
    const char* path=std::getenv("TERMCAP"); const std::string database=path!=nullptr&&path[0]=='/'?path:"/etc/termcap";
    std::ifstream in(database); if(!in) return Error{ENOENT,"termcap database not found"}; std::string line, combined{};
    while(std::getline(in,line)) { if(!line.empty()&&line.back()=='\\') {combined+=line.substr(0,line.size()-1);continue;} combined+=line; if(combined.rfind(name+"|",0)==0||combined.rfind(name+":",0)==0) return load_from(combined); combined.clear(); } return Error{ENOENT,"termcap entry not found"};
}
// Termcap.man: tgoto uses %r, %i, %d, %2, %3, %+x and %% substitutions.
[[nodiscard]] inline auto tgoto(const std::string_view pattern, int column, int row) -> std::string {
    int values[2]{column,row}; int index=0; std::string out{};
    for(std::size_t i=0;i<pattern.size();++i) { if(pattern[i]!='%'||++i==pattern.size()){out+=pattern[i];continue;} const char op=pattern[i];
        if(op=='r'){std::swap(values[0],values[1]);} else if(op=='i'){++values[0];++values[1];} else if(op=='n'){values[0]^=0140;values[1]^=0140;} else if(op=='%')out+='%'; else { const int value=values[index++%2]; if(op=='d')out+=std::to_string(value); else if(op=='2'||op=='3'){auto s=std::to_string(value);out.append(static_cast<std::size_t>(op-'0')>s.size()?static_cast<std::size_t>(op-'0')-s.size():0,'0');out+=s;} else if(op=='+'&&i+1<pattern.size())out+=static_cast<char>(value+pattern[++i]); } }
    return out;
}
[[nodiscard]] inline auto from_terminfo(const terminfo::Entry& entry) -> Entry {
    // NOTE(agent): The manifest does not supply the terminfo capability-index mapping.
    Entry converted{}; converted.names=entry.names; return converted;
}
}  // namespace ttytk::termcap
