#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <array>
#include <cwctype>
#include <cctype>
#include <streambuf>
#include <memory>
#include <chrono>
#include <ctime>
namespace fs = std::filesystem;
// Writes every console message to both the terminal and a durable text file.
class TeeBuffer : public std::streambuf {
public:
  TeeBuffer(std::streambuf* terminal, std::streambuf* file) : terminal_(terminal), file_(file) {}
protected:
  int overflow(int ch) override {
    if (ch == traits_type::eof()) return traits_type::not_eof(ch);
    const char c = static_cast<char>(ch);
    return terminal_->sputc(c) == traits_type::eof() || file_->sputc(c) == traits_type::eof()
      ? traits_type::eof() : ch;
  }
  std::streamsize xsputn(const char* s, std::streamsize n) override {
    auto a = terminal_->sputn(s, n);
    auto b = file_->sputn(s, n);
    return std::min(a, b);
  }
  int sync() override { return (terminal_->pubsync() == 0 && file_->pubsync() == 0) ? 0 : -1; }
private:
  std::streambuf* terminal_;
  std::streambuf* file_;
};
class SessionLog {
public:
  SessionLog() {
    std::wstring exe(MAX_PATH, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, exe.data(), (DWORD)exe.size());
    if (n > 0 && n < exe.size()) exe.resize(n);
    else exe = L"OAFExtractor.exe";
    auto directory = fs::path(exe).parent_path();
    if (directory.empty()) directory = fs::current_path();
    path_ = directory / "OAFExtractor.log";
    log_.open(path_, std::ios::binary | std::ios::app);
    if (!log_) {
      wchar_t temp[MAX_PATH]{};
      DWORD len = GetTempPathW(MAX_PATH, temp);
      if (len > 0 && len < MAX_PATH) {
        path_ = fs::path(temp) / "OAFExtractor.log";
        log_.clear(); log_.open(path_, std::ios::binary | std::ios::app);
      }
    }
    if (log_) {
      out_ = std::make_unique<TeeBuffer>(std::cout.rdbuf(), log_.rdbuf());
      err_ = std::make_unique<TeeBuffer>(std::cerr.rdbuf(), log_.rdbuf());
      oldOut_ = std::cout.rdbuf(out_.get());
      oldErr_ = std::cerr.rdbuf(err_.get());
      std::time_t now = std::time(nullptr);
      std::cout << "\n=========== OAFExtractor " << std::ctime(&now)
                << "Log: " << path_ << "\n" << std::flush;
    } else std::cerr << "WARNING: Could not open OAFExtractor.log near EXE or in Temp.\n";
  }
  ~SessionLog() {
    std::cout.flush(); std::cerr.flush();
    if (oldOut_) std::cout.rdbuf(oldOut_);
    if (oldErr_) std::cerr.rdbuf(oldErr_);
  }
  const fs::path& path() const { return path_; }
private:
  fs::path path_;
  std::ofstream log_;
  std::unique_ptr<TeeBuffer> out_, err_;
  std::streambuf *oldOut_=nullptr, *oldErr_=nullptr;
};

struct Entry { uint32_t hash; uint64_t offset, size; std::string name; };
template<class T> bool read(std::istream& f, T& v) { return bool(f.read(reinterpret_cast<char*>(&v),sizeof(v))); }
bool validPath(const std::string& s) {
    if(s.empty() || s.size()>1024 || s[0]=='/' || s[0]=='\\' || s.find(':')!=std::string::npos) return false;
    std::string part;
    for(char c:s){ if(c=='/'||c=='\\'){if(part==".." || part.empty()) return false;part.clear();} else {if((unsigned char)c<32)return false;part+=c;} }
    return !part.empty() && part!=".." && part!=".";
}
int extract(const fs::path& archive, const fs::path& dest, bool guiOnly) {
  std::cout << "Opening archive: " << archive << "\n" << std::flush;
  std::ifstream f(archive,std::ios::binary); if(!f){std::cerr<<"Cannot open: "<<archive<<"\n";return 1;}
  std::error_code sizeError;
  uint64_t total=fs::file_size(archive, sizeError);
  if (sizeError) { std::cerr << "Cannot query archive size: " << sizeError.message() << "\n" << std::flush; return 1; }
  std::cout << "Archive size: " << total << " bytes\n" << std::flush; char magic[4]; uint32_t version,meta,dataEnd,reserved,count;
  if(!f.read(magic,4) || std::string(magic,4)!="OAF!" || !read(f,version)||!read(f,meta)||!read(f,dataEnd)||!read(f,reserved)||!read(f,count)) {std::cerr<<"Invalid OAF header\n";return 1;}
  std::cout << "OAF header: version=" << version << " meta=" << meta << " dataEnd=" << dataEnd << " reserved=" << reserved << " entries=" << count << "\n" << std::flush;
  // The verified Stick of Truth OAF variant contains 20-byte entries followed by a NUL-delimited filename list.
  if(count==0 || count>2000000 || uint64_t(count)*20+24>total || dataEnd>=total || dataEnd<uint64_t(count)*20+24) { std::cerr<<"Unsupported OAF layout\n";return 1; }
  std::cout << "Reading " << count << " archive entries...\n" << std::flush;
  std::vector<Entry> entries; entries.reserve(count);
  for(uint32_t i=0;i<count;i++){Entry e{};if(!read(f,e.hash)||!read(f,e.offset)||!read(f,e.size) || e.offset<uint64_t(count)*20+24 || e.offset>dataEnd || e.size>dataEnd-e.offset){std::cerr<<"Invalid entry "<<i<<"\n";return 1;}entries.push_back(e);}
  std::cout << "Reading filename table...\n" << std::flush;
  f.seekg(dataEnd);if(!f){std::cerr<<"Cannot seek filename table\n";return 1;}
  for(auto& e:entries){char c=0;while(f.get(c)&&c!='\0'){if(e.name.size()>=1024){std::cerr<<"Oversized path\n";return 1;}e.name.push_back(c);}if(!f || !validPath(e.name)){std::cerr<<"Invalid filename table entry\n";return 1;}}
  std::cout<<"Archive: "<<archive.filename().string()<<" | files: "<<count<<" | indexed data: "<<dataEnd<<" bytes\n" << std::flush;
  std::error_code ec;fs::create_directories(dest,ec);if(ec){std::cerr<<"Output directory error: "<<ec.message()<<"\n";return 1;}
  std::cout << "Starting extraction...\n" << std::flush;
  uint32_t extracted=0;std::vector<char> buf(64 * 1024); // Heap allocation: avoid Win32 1 MB stack overflow.
  for(const auto& e:entries){
    fs::path rel=fs::u8path(e.name); if(guiOnly){auto s=e.name;std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});if(s.find("gui") == std::string::npos && s.find("menu") == std::string::npos && s.find("interface") == std::string::npos && s.find("scaleform") == std::string::npos)continue;}
    fs::path out=dest/rel;fs::create_directories(out.parent_path(),ec);if(ec){std::cerr<<"Cannot make folder "<<out.parent_path()<<"\n";return 1;}
    if(fs::exists(out)){std::cout<<"Skipping existing: "<<e.name<<"\n";continue;}
    f.clear();f.seekg(e.offset);if(!f){std::cerr<<"Read seek failure\n";return 1;}
    std::ofstream g(out,std::ios::binary);if(!g){std::cerr<<"Cannot write "<<out<<"\n";return 1;}
    uint64_t left=e.size;while(left){auto n=std::streamsize(std::min<uint64_t>(left,buf.size()));if(!f.read(buf.data(),n)){std::cerr<<"Read failure: "<<e.name<<"\n";return 1;}g.write(buf.data(),n);if(!g){std::cerr<<"Write failure: "<<e.name<<"\n";return 1;}left-=n;}
    ++extracted;if(extracted%100==0)std::cout<<"Extracted "<<extracted<<" files\n" << std::flush;
  }
  std::cout<<"Done. Extracted "<<extracted<<" files to "<<dest<<"\n";return 0;
}
// Explorer launches console applications in a new console. Keep it open so
// drag-and-drop users can see extraction progress and any errors.
struct ExitPause {
  bool enabled = false;
  ~ExitPause() {
    if (enabled) {
      std::cout << "\nLog saved as OAFExtractor.log (beside the EXE, or in Windows Temp if unwritable).\nPress Enter to close..." << std::flush;
      std::string line;
      std::getline(std::cin, line);
    }
  }
};

bool isOaf(const fs::path& p) {
  auto ext = p.extension().wstring();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return wchar_t(towlower(c)); });
  return ext == L".oaf";
}

int wmain(int argc, wchar_t** argv) {
  SessionLog session;
  std::cout << "Arguments (" << argc-1 << "):\n";
  for (int i=1; i<argc; ++i) std::cout << "  " << fs::path(argv[i]) << "\n";
  DWORD processIds[2]{};
  ExitPause pause;
  pause.enabled = GetConsoleProcessList(processIds, 2) == 1;
  try {
    bool gui = false, all = false;
    std::vector<fs::path> inputs;
    for (int i = 1; i < argc; ++i) {
      const std::wstring arg(argv[i]);
      if (arg == L"--gui") gui = true;
      else if (arg == L"--all") all = true;
      else if (arg == L"--help" || arg == L"-h") {
        std::cout << "Drag one or more .oaf files onto OAFExtractor.exe, or run:\n"
                  << "OAFExtractor.exe [--gui] [--all] [archive.oaf ...] [folder ...]\n";
        return 0;
      } else if (!arg.empty() && arg[0] == L'-') {
        std::cerr << "Unknown option: " << fs::path(arg) << "\n";
        return 2;
      } else inputs.push_back(fs::absolute(fs::path(arg)));
    }
    if (inputs.empty()) inputs.push_back(fs::current_path());

    std::vector<fs::path> archives;
    for (const auto& input : inputs) {
      if (fs::is_regular_file(input)) {
        if (!isOaf(input)) {
          std::cerr << "Not an OAF file: " << input << "\n";
          return 2;
        }
        archives.push_back(input);
      } else if (fs::is_directory(input)) {
        for (const auto& item : fs::directory_iterator(input)) {
          if (!item.is_regular_file() || !isOaf(item.path())) continue;
          auto filename = item.path().filename().wstring();
          std::transform(filename.begin(), filename.end(), filename.begin(),
                         [](wchar_t c) { return wchar_t(towlower(c)); });
          if (all || filename == L"data_archive.oaf" || filename == L"data_localized_en.oaf")
            archives.push_back(item.path());
        }
      } else {
        std::cerr << "File or folder not found: " << input << "\n";
        return 2;
      }
    }
    std::sort(archives.begin(), archives.end());
    archives.erase(std::unique(archives.begin(), archives.end()), archives.end());
    if (archives.empty()) {
      std::cerr << "No OAF archives found. Drag any .oaf file onto this EXE, "
                   "or supply its full path.\n";
      return 1;
    }
    int failures = 0;
    for (const auto& archive : archives) {
      // Always write next to the chosen archive, regardless of the EXE's location.
      const fs::path target = archive.parent_path() / "ExtractedOAF" / archive.stem();
      std::cout << "\nProcessing: " << archive << "\nOutput: " << target << "\nMode: " << (gui ? "GUI-only" : "all files") << "\n" << std::flush;
      if (extract(archive, target, gui) != 0) ++failures;
    }
    std::cout << "\nFinished: " << (archives.size() - failures) << " succeeded, "
              << failures << " failed.\n";
    return failures ? 1 : 0;
  } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n";
    return 1;
  }
}
