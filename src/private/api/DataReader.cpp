#include "vuldir/DataReader.hpp"

using namespace vd;

DataReader::DataReader(const Desc& desc)
{
  if(desc.fileReader) m_fileReader = desc.fileReader;
  else
    m_fileReader = [](const fs::path& path, const fs::path* basePath) {
      if(basePath) return getBytes((*basePath) / path);
      else
        return getBytes(path);
    };
}

data::Image
DataReader::ReadImage(std::istream& src, const ImageOptions& options)
{
  if(isPng(src)) return readPng(src, options);

  throw std::runtime_error("DataReader: Unsupported image format");
}

data::Image
DataReader::ReadImage(const fs::path& path, const ImageOptions& options)
{
  auto fsOpt = options;
  if(fsOpt.uri.empty()) fsOpt.uri = pathToStr(path);

  std::ifstream src(path, std::ios::binary);
  if(!src)
    throw makeError<std::runtime_error>(
      "DataReader: cannot access file %s", path.u8string().c_str());

  return ReadImage(src, fsOpt);
}

data::Image
DataReader::ReadImage(Span<u8 const> src, const ImageOptions& options)
{
  IMemoryStream stream(src);
  return ReadImage(stream, options);
}

data::Model
DataReader::ReadModel(std::istream& src, const ModelOptions& options)
{
  if(isBinaryGLTF(src)) return readBinaryGLTF(src, options);
  else if(isGLTF(src))
    return readGLTF(src, options);

  throw std::runtime_error("DataReader: Unsupported asset format");
}

data::Model
DataReader::ReadModel(const fs::path& path, const ModelOptions& options)
{
  auto fsOpt = options;
  if(!fsOpt.uri) fsOpt.uri = pathToStr(path);

  if(!fsOpt.basePath) fsOpt.basePath = path.parent_path();

  std::ifstream src(path, std::ios::binary);
  if(!fsOpt.basePath) fsOpt.basePath = path.parent_path();
  return ReadModel(src, fsOpt);
}

data::Model
DataReader::ReadModel(Span<u8 const> src, const ModelOptions& options)
{
  IMemoryStream stream(src);
  return ReadModel(stream, options);
}
