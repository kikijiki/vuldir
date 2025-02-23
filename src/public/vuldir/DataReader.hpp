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
    // If you have an asset cache you can filter out assets that you
    // already have loaded. In that case data load for that buffer will
    // be skipped. You can then later use the uri to manually assign the
    // buffer from your cache.
    UriFilter uriFilter;

    // Leave empty to use the default filesystem reader.
    // Can be set to read from custom data sources like archives and
    // data packages. Since the uri can be relative, the second argument
    // is an optional base path. Can also be used for raw data caching.
    FileReader fileReader;
  };

  struct ImageOptions {
    // Use to set or override the uri.
    // When loading from the filesystem it will be the file path.
    // Otherwise it will be empty by default;
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
