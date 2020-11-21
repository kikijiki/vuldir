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

DataReader::ImageData
DataReader::ReadImage(std::istream& src, const ImageOptions& options)
{
  if(isPng(src)) return readPng(src, options);

  throw std::runtime_error("DataReader: Unsupported image format");
}

DataReader::ImageData
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

DataReader::ImageData
DataReader::ReadImage(Span<u8 const> src, const ImageOptions& options)
{
  IMemoryStream stream(src);
  return ReadImage(stream, options);
}

// data::Model
// DataReader::ReadModel(std::istream& src, const ModelOptions& options)
//{
//  if(isBinaryGLTF(src)) return readBinaryGLTF(src, options);
//  else if(isGLTF(src))
//    return readGLTF(src, options);
//
//  throw std::runtime_error("DataReader: Unsupported asset format");
//}
//
// data::Model
// DataReader::ReadModel(const fs::path& path, const ModelOptions&
// options)
//{
//  if(options.uri) options.uri = pathToStr(path);
//
//  if(!options.basePath) options.basePath = path.parent_path();
//
//  std::ifstream src(path, std::ios::binary);
//  if(!options.basePath) options.basePath = path.parent_path();
//  return ReadModel(src, options);
//}
//
// data::Model
// DataReader::ReadModel(Span<u8 const> src, const ModelOptions&
// options)
//{
//  IMemoryStream stream(src);
//  return ReadModel(stream, options);
//}
