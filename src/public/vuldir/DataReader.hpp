#pragma once

#include "vuldir/Data.hpp"
#include "vuldir/api/Api.hpp"
#include "vuldir/core/Core.hpp"

namespace vd {

class DataReader
{
public:
  using UriFilter  = std::function<bool(Strv)>;
  using FileReader = std::function<Arr<u8>(
    const fs::path& path, const fs::path* basePath)>;

  struct Desc {
    // Return true for uris already cached by the caller to skip loading
    // their data. The buffer can be assigned later from the uri.
    UriFilter uriFilter;

    // Custom reader (archives, packages, caching). Empty uses the
    // filesystem. basePath resolves relative uris.
    FileReader fileReader;
  };

  struct ImageOptions {
    // Overrides the uri. Defaults to the file path when loading from the
    // filesystem, empty otherwise.
    Str uri;

    u8 alphaPadding = 0xff;
  };

  struct ModelOptions {
    Opt<Str>      uri;
    Opt<fs::path> basePath;
  };

public:
  DataReader(const Desc& desc = {});

public:
  data::Image ReadImage(std::istream& src, const ImageOptions& options);
  data::Image
  ReadImage(Span<u8 const> src, const ImageOptions& options);
  data::Image
  ReadImage(const fs::path& path, const ImageOptions& options);

  data::Model ReadModel(std::istream& src, const ModelOptions& options);
  data::Model
  ReadModel(Span<u8 const> src, const ModelOptions& options);
  data::Model
  ReadModel(const fs::path& path, const ModelOptions& options);

private:
  bool        isPng(std::istream& str);
  data::Image readPng(std::istream& str, const ImageOptions& options);

  bool        isHdr(std::istream& str);
  data::Image readHdr(std::istream& str, const ImageOptions& options);

  bool isGLTF(std::istream& src);
  bool isBinaryGLTF(std::istream& src);

  data::Model
  readGLTF(std::istream& src, const ModelOptions& options = {});
  data::Model
  readBinaryGLTF(std::istream& src, const ModelOptions& options = {});

private:
  UriFilter  m_uriFilter;
  FileReader m_fileReader;
};

} // namespace vd
