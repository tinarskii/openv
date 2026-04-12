#pragma once
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <cstdint>

struct FaceParams {
  uint32_t version = 0;
  float angleX   = 0.f;
  float angleY   = 0.f;
  float angleZ   = 0.f;
  float mouthOpen = 0.f;
  float mouthForm = 0.f;
  float eyeBallX = 0.f;
  float eyeBallY = 0.f;
  float eyeOpenL = 1.f;
  float eyeOpenR = 1.f;
  float browL = 0.f;
  float browR = 0.f;
};

class SharedMemory {
public:
#if defined(_WIN32)
  static constexpr const wchar_t* SHM_NAME = L"Local\\face_params";
#else
  static constexpr const char* SHM_NAME = "/face_params";
#endif

  static FaceParams* openReader() {
#if defined(_WIN32)
    HANDLE& mapping = mappingHandle();
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(sizeof(FaceParams)), SHM_NAME);
    if (!mapping) return nullptr;
    void* ptr = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(FaceParams));
    if (!ptr) {
      CloseHandle(mapping);
      mapping = nullptr;
      return nullptr;
    }
    return static_cast<FaceParams*>(ptr);
#else
    int fd = shm_open(SHM_NAME, O_RDONLY, 0666);
    if (fd < 0) return nullptr;
    void* ptr = mmap(nullptr, sizeof(FaceParams), PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    return (ptr == MAP_FAILED) ? nullptr : static_cast<FaceParams*>(ptr);
#endif
  }

  static void close(FaceParams* ptr) {
#if defined(_WIN32)
    if (ptr) UnmapViewOfFile(ptr);
    HANDLE& mapping = mappingHandle();
    if (mapping) {
      CloseHandle(mapping);
      mapping = nullptr;
    }
#else
    if (ptr) munmap(ptr, sizeof(FaceParams));
#endif
  }

private:
#if defined(_WIN32)
  static HANDLE& mappingHandle() {
    static HANDLE mapping = nullptr;
    return mapping;
  }
#endif
};
