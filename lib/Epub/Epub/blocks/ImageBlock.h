#pragma once
#include "Block.h"
#include <SdFat.h>
#include <memory>
#include <string>

class GfxRenderer;

class ImageBlock final : public Block {
  std::string bmpPath;
  uint16_t width;
  uint16_t height;

public:
  ImageBlock(std::string bmpPath, uint16_t width, uint16_t height)
      : bmpPath(std::move(bmpPath)), width(width), height(height) {}
  ~ImageBlock() override = default;

  void layout(GfxRenderer &renderer) override {}
  BlockType getType() override { return IMAGE_BLOCK; }
  bool isEmpty() override { return bmpPath.empty(); }

  void render(const GfxRenderer &renderer, int x, int y) const;

  uint16_t getWidth() const { return width; }
  uint16_t getHeight() const { return height; }

  bool serialize(FsFile &file) const;
  static std::unique_ptr<ImageBlock> deserialize(FsFile &file);
};
