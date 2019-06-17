
#pragma once

#include <windows.h>
#include <gdiplus.h>

#include <cassert>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

#include "spinlock.h"

struct VectorPicture {
  struct Line {
    Line() = default;
    std::size_t start{}, length{};
    Gdiplus::Color color;
  };

  std::vector<Gdiplus::Point> points{};
  std::vector<Line> lines{};
  std::size_t current_point{}, start_line{}, end_line{};

  VectorPicture() = default;

  VectorPicture(const std::size_t points_count, const std::size_t lines_count) {
    init(points_count, lines_count);
  }

  VectorPicture(const VectorPicture& other) {
    InitFromOther(other);
  }

  VectorPicture& operator=(const VectorPicture& other) {
    InitFromOther(other);
    return *this;
  }

  void InitFromOther(const VectorPicture& other) {
    this->points = other.points;
    this->lines = other.lines;
    this->current_point = other.current_point;
    this->start_line = other.start_line;
    this->end_line = other.end_line;
  }

  void init(const std::size_t points_count, const std::size_t lines_count) {
    points.reserve(points_count);
    points.assign(points_count, Gdiplus::Point{ 0, 0 });
    lines.reserve(lines_count);
    lines.assign(lines_count, Line{});
  }

  void clear() {
    for (auto& line : lines) {
      line.length = 0;
      line.start = 0;
    }
    start_line = end_line = 0;
  }

  std::size_t TotalSize() const {
    return sizeof(current_point) + sizeof(start_line) + sizeof(end_line) +
      sizeof(points.size()) * sizeof(Gdiplus::Point) +
      sizeof(lines.size()) * sizeof(Line);
  }//*/

  void AddPoint(const Gdiplus::Point& point) {
    if (current_point == lines.at(start_line).start) {
      DecrementLineFromLeftSide(start_line);
      if (lines.at(start_line).length == 0 and start_line != end_line) {
        ++start_line;
        if (start_line >= lines.size()) {
          start_line = 0;
        }
      }
    }

    points.at(current_point) = point;
    if (lines.at(end_line).length < points.size()) {
      ++lines.at(end_line).length;
    }

    ++current_point;
    if (current_point >= points.size()) {
      current_point = 0;
    }
  }

  void AddLine(const Gdiplus::Color& color) {
    ++end_line;
    if (end_line >= lines.size()) {
      end_line = 0;
    }
    if (end_line == start_line) {
      ++start_line;
      if (start_line >= lines.size()) {
        start_line = 0;
      }
    }

    auto & line{ lines.at(end_line) };
    line.length = 0;
    line.start = current_point;
    line.color = color;
  }

private:
  void DecrementLineFromLeftSide(const std::size_t line_index) {
    auto & line = lines.at(line_index);
    if (line.length > 0) {
      --line.length;
      ++line.start;
      if (line.start >= points.size()) {
        line.start = 0;
      }
    }
  }
};

std::ostream& operator<<(std::ostream& os, const Gdiplus::Color& c) {
  os << c.GetValue();
  return os;
}

std::istream& operator>>(std::istream& is, Gdiplus::Color& c) {
  Gdiplus::ARGB argb{};
  is >> argb;
  c.SetValue(argb);
  return is;
}

std::ostream& operator<<(std::ostream& os, const VectorPicture::Line& l) {
  os << l.start << l.length << l.color;
  return os;
}

std::istream& operator>>(std::istream& is, VectorPicture::Line& l) {
  is >> l.start >> l.length >> l.color;
  return is;
}

std::ostream& operator<<(std::ostream& os, const Gdiplus::Point& p) {
  os << p.X << p.Y;
  return os;
}

std::istream& operator>>(std::istream& is, Gdiplus::Point& p) {
  is >> p.X >> p.Y;
  return is;
}

std::ostream& operator<<(std::ostream& os, const VectorPicture& p) {
  os << p.TotalSize() << p.current_point << p.start_line << p.end_line <<
    p.points.size() << p.lines.size();
  for (auto& l : p.lines) {
    os << l;
  }
  for (auto& pt : p.points) {
    os << pt;
  }
  return os;
}

std::istream& operator>>(std::istream& is, VectorPicture& p) {
  std::size_t total_size{}, points_size{}, lines_size{};
  is >> total_size >> p.current_point >> p.start_line >> p.end_line >>
    points_size >> lines_size;
  p.lines.reserve(lines_size);
  for (std::size_t i{}; i < lines_size; ++i) {
    VectorPicture::Line l;
    is >> l;
    p.lines.push_back(l);
  }
  p.points.reserve(points_size);
  for (std::size_t i{}; i < points_size; ++i) {
    Gdiplus::Point pt;
    is >> pt;
    p.points.push_back(pt);
  }
  return is;
}


class RasterPicture {
public:
  RasterPicture() = default;
  RasterPicture(const POINT& size) {
    create(size);
  }
  ~RasterPicture() {
    ::delete bitmap_;
    bitmap_ = nullptr;
  }

  Gdiplus::Bitmap * GetBitmap() {
    return bitmap_;
  }

  void create(const POINT& size) {
    ::delete bitmap_;
    std::cout << sizeof(Gdiplus::Bitmap) << std::endl;
    bitmap_ = ::new Gdiplus::Bitmap{size.x, size.y, PixelFormat32bppRGB};
    assert(bitmap_);
    clear();
  }
 
  void clear() {
    if (bitmap_) {
      Gdiplus::Graphics graphics(bitmap_);
      graphics.Clear(Gdiplus::Color(255, 255, 255, 255));
    }
  }

private:
  Gdiplus::Bitmap * bitmap_{};
};

class SharedPicture {
public:
  SharedPicture() = default;
  SharedPicture(const POINT& bitmap_size) : raster_{ bitmap_size } { }
  SharedPicture(const std::size_t points_count, 
                const std::size_t lines_count,
                const POINT& bitmap_size) : 
    vector_{points_count, lines_count}, raster_{bitmap_size} { }

  void SetVectorPicture(const VectorPicture& vector_picture) {
    std::lock_guard<SpinLock> lock{this->vector_lock_};
    this->vector_ = vector_picture;
  }

  void OutputVectorPictureToOstream(std::ostream& os) {
    std::lock_guard<SpinLock> losk{this->vector_lock_};
    os << this->vector_;
  }

  void AddPoint(const Gdiplus::Point& point) {
    std::lock_guard<SpinLock> lock{ vector_lock_ };
    vector_.AddPoint(point);
  }

  void AddLine(const Gdiplus::Color& color) {
    std::lock_guard<SpinLock> lock{ vector_lock_ };
    vector_.AddLine(color);
  }
 
  void DrawVectorPicture(Gdiplus::Graphics& graphics) {
    graphics.Clear(Gdiplus::Color(255, 255, 255, 255));
    //graphics.SetSmoothingMode(Gdiplus::SmoothingMode::SmoothingModeNone);
    //graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
    //graphics.Flush(Gdiplus::FlushIntention::FlushIntentionSync);

    std::lock_guard<SpinLock> lock{vector_lock_};
    for (auto & line : vector_.lines) {
      if (line.length != 0) {
        Gdiplus::Pen pen(line.color, 4.f);
        if (line.length <= vector_.points.size() - line.start) {
          graphics.DrawLines(&pen, vector_.points.data() + line.start, line.length);
        } else {
          std::size_t right_part{ vector_.points.size() - line.start };
          graphics.DrawLines(&pen, vector_.points.data() + line.start, right_part);
          graphics.DrawLine(&pen, vector_.points.back(), vector_.points.front());
          graphics.DrawLines(&pen, vector_.points.data(), line.length - right_part);
        }
      }
    }
  }

  void DrawRasterPicture(Gdiplus::Graphics& graphics) {
    std::lock_guard<SpinLock> lock{raster_lock_};
    auto * bitmap = raster_.GetBitmap();
    Gdiplus::Rect size{0, 0, (INT)bitmap->GetWidth(), (INT)bitmap->GetHeight()};
    graphics.DrawImage(bitmap, size, 0, 0,
      (int)bitmap->GetWidth(),
      (int)bitmap->GetHeight(),
      Gdiplus::UnitPixel);
  }

  void Rasterize() {
    std::lock_guard<SpinLock> lock{ raster_lock_ };
    auto * bitmap = raster_.GetBitmap();
    Gdiplus::Graphics graphics{bitmap};
    DrawVectorPicture(graphics);
  }

  void Clear() {
    std::lock_guard<SpinLock> lock{vector_lock_};
    vector_.clear();
  }

//private:
  VectorPicture vector_;
  RasterPicture raster_;
  SpinLock vector_lock_, raster_lock_;
};
