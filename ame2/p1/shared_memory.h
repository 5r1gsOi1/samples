#pragma once

#include <windows.h>
#include <string>
#include <stdexcept>
#include <streambuf>

class SharedMemory {
public:
  struct errors {
    struct General : public std::runtime_error {
      General(const DWORD error_code = 0) :
        std::runtime_error{ "SharedMemory error " + std::to_string(error_code) } {}
      DWORD error_code{};
    };

    struct CouldNotCreateFile : public General {
      CouldNotCreateFile(const DWORD error_code = 0) : General{ error_code } {}
    };
    struct CouldNotMapFile : public General {
      CouldNotMapFile(const DWORD error_code = 0) : General{ error_code } {}
    };
    struct CouldNotOpenMapFile : public General {
      CouldNotOpenMapFile(const DWORD error_code = 0) : General{ error_code } {}
    };
    struct CouldNotMapViewBuffer : public General {
      CouldNotMapViewBuffer(const DWORD error_code = 0) : General{ error_code } {}
    };
    struct FileNotMapped : public General {
      FileNotMapped() : General{ 0 } {}
    };
  };

  SharedMemory(const std::string& file_name,
    const std::string& mapped_file_name,
    const std::size_t mapped_file_size) :
    file_name_{ file_name }, mapped_file_name_{ mapped_file_name },
    mapped_file_size_{ mapped_file_size } { }

  ~SharedMemory() { fini(); }

  void Create() {
    fini();
    file_ = CreateFile(file_name_.c_str(), GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
      CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_SEQUENTIAL_SCAN,
      NULL);
    if (file_ == NULL) {
      fini();
      throw errors::CouldNotCreateFile{ GetLastError() };
    }

    mapped_file_ = CreateFileMapping(file_, NULL, PAGE_READWRITE, 0,
      this->mapped_file_size_, mapped_file_name_.c_str());
    if (mapped_file_ == NULL) {
      fini();
      throw errors::CouldNotMapFile{ GetLastError() };
    }
  }

  void Open() {
    fini();
    mapped_file_ = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, mapped_file_name_.c_str());
    if (mapped_file_ == NULL) {
      throw errors::CouldNotOpenMapFile{ GetLastError() };
    }
  }

  bool IsOpened() const {
    return mapped_file_ != NULL;
  }

  bool IsMapped() const {
    return buffer_is_mapped_;
  }

  void fini() {
    if (buffer_is_mapped_) {
      UnmapViewOfFile(mapped_buffer_);
      mapped_buffer_ = NULL;
      buffer_is_mapped_ = false;
    }
    if (mapped_file_) {
      CloseHandle(mapped_file_);
      mapped_file_ = NULL;
    }
    if (file_) {
      CloseHandle(file_);
      file_ = NULL;
    }
  }
  std::size_t GetMappedSize() {
    return this->mapped_file_size_;
  }

  void MapViewBuffer() {
    MapViewBuffer(this->mapped_file_size_);
  }

  void MapViewBuffer(const DWORD view_buffer_size) {
    if (mapped_file_ == NULL) {
      throw errors::FileNotMapped{};
    }
    if (buffer_is_mapped_) {
      UnmapViewOfFile(mapped_buffer_);
      mapped_buffer_ = NULL;
    } else {
      mapped_buffer_ = (LPTSTR)MapViewOfFile(mapped_file_, FILE_MAP_ALL_ACCESS,
        0, 0, view_buffer_size);
      if (mapped_buffer_ == NULL) {
        throw errors::CouldNotMapViewBuffer{ GetLastError() };
      }
      buffer_is_mapped_ = true;
    }
  }

  LPCTSTR GetViewBuffer() {
    if (buffer_is_mapped_) {
      return mapped_buffer_;
    } else {
      return NULL;
    }
  }

private:
  std::string file_name_;
  std::string mapped_file_name_;
  std::size_t mapped_file_size_{};

  HANDLE file_{}, mapped_file_{};
  LPCTSTR mapped_buffer_;
  bool buffer_is_mapped_{ false };
};

// https://stackoverflow.com/questions/7781898/get-an-istream-from-a-char
struct membuf : std::streambuf {
  membuf(char* begin, char* end) {
    this->setg(begin, begin, end);
  }
};

// https://stackoverflow.com/questions/1494182/setting-the-internal-buffer-used-by-a-standard-stream-pubsetbuf
template <typename char_type>
struct ostreambuf : public std::basic_streambuf<char_type, std::char_traits<char_type>> {
  ostreambuf(char_type* buffer, std::streamsize bufferLength) {
    // set the "put" pointer the start of the buffer and record it's length.
    std::basic_streambuf<char_type, std::char_traits<char_type>>::setp(buffer, buffer + bufferLength);
  }
};
