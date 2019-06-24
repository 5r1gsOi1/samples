
#pragma once

#include <windows.h>
#include <gdiplus.h>

#include <cassert>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

#include "spinlock.h"
#include "jzon.h"


struct VectorPicture {
  struct errors {
    struct NotParsedFromJson : public std::runtime_error {
      NotParsedFromJson() : std::runtime_error{ "" } {}
    };
    struct NotParsedNumberFromJson : public NotParsedFromJson {};
    struct NotParsedRootFromJson : public NotParsedFromJson {};

    struct NotParsedPointsRootFromJson : public NotParsedFromJson {};
    struct NotParsedPointsElementFromJson : public NotParsedFromJson {};

    struct NotParsedLinesRootFromJson : public NotParsedFromJson {};
    struct NotParsedLinesElementFromJson : public NotParsedFromJson {};
  };

  struct Line {
    Line() = default;
    std::size_t start{}, length{};
    Gdiplus::Color color{};
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
      while (lines.at(start_line).length == 0 and start_line != end_line) {
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

  std::string SerializeToJson() {
    jzon::Writer writer;
    jzon::Node root{ jzon::object() };
    root.add("current_point", current_point);
    root.add("start_line", start_line);
    root.add("end_line", end_line);
    root.add("points_size", points.size());
    root.add("lines_size", lines.size());
    {
      jzon::Node points{jzon::array()};
      for (auto& p : this->points) {
        jzon::Node point{jzon::array()};
        point.add(p.X);
        point.add(p.Y);
        points.add(point);
      }
      root.add("points", points);
    }
    {
      jzon::Node lines{ jzon::array() };
      for (auto& l : this->lines) {
        jzon::Node line{ jzon::array() };
        line.add(l.start);
        line.add(l.length);
        line.add(static_cast<unsigned long long>(l.color.GetValue()));
        lines.add(line);
      }
      root.add("lines", lines);
    }

    std::string result;
    writer.writeString(root, result);
    return result;
  }

  std::size_t GetNumberFromJzonNode(jzon::Node& node, const std::string& name) {
    auto number_node {node.get(name)};
    if (not number_node.isValid() or not number_node.isNumber()) {
      throw errors::NotParsedNumberFromJson{};
    }
    return number_node.toInt();
  }

  void LoadFromJson(const std::string& json_string) {
    jzon::Parser parser{};
    jzon::Node root {parser.parseString(json_string)};
    if (not root.isValid()) {
      throw errors::NotParsedRootFromJson{};
    }
    auto new_current_point{ GetNumberFromJzonNode(root, "current_point") };
    auto new_start_line{ GetNumberFromJzonNode(root, "start_line") };
    auto new_end_line{ GetNumberFromJzonNode(root, "end_line") };
    std::size_t points_size{ GetNumberFromJzonNode(root, "points_size") };
    std::size_t lines_size{ GetNumberFromJzonNode(root, "lines_size") };
    
    std::vector<Gdiplus::Point> new_points(points_size);

    auto points_node {root.get("points")};
    if (not points_node.isValid() or not points_node.isArray() or 
      points_node.getCount() != points_size) {
      throw errors::NotParsedPointsRootFromJson{};
    }
    for (std::size_t i{}; i < points_node.getCount(); ++i) {
      auto point{points_node.get(i)};
      if (not point.isValid() or not point.isArray() or point.getCount() != 2) {
        throw errors::NotParsedPointsElementFromJson{};
      }
      new_points.at(i) = Gdiplus::Point{point.get(0).toInt(), point.get(1).toInt()};
    }

    std::vector<Line> new_lines(lines_size);

    auto lines_node{ root.get("lines") };
    if (not lines_node.isValid() or not lines_node.isArray() or
      lines_node.getCount() != lines_size) {
      throw errors::NotParsedLinesRootFromJson{};
    }
    for (std::size_t i{}; i < lines_node.getCount(); ++i) {
      auto line{ lines_node.get(i) };
      if (not line.isValid() or not line.isArray() or line.getCount() != 3) {
        throw errors::NotParsedLinesElementFromJson{};
      }
      new_lines.at(i).start = line.get(0).toInt();
      new_lines.at(i).length = line.get(1).toInt();
      new_lines.at(i).color.SetValue(0xFF000000/*line.get(2).toLong()*/);
    }

    clear();
    start_line = new_start_line;
    end_line = new_end_line;
    current_point = new_current_point;

    points = std::move(new_points);
    lines = std::move(new_lines);
  }

private:
  void DecrementLineFromLeftSide(const std::size_t line_index) {
    auto& line {lines.at(line_index)};
    if (line.length > 0) {
      --line.length;
      ++line.start;
      if (line.start >= points.size()) {
        line.start = 0;
      }
    }
  }
};

class RasterPicture {
public:
  RasterPicture() = default;
  RasterPicture(const POINT& size) {
    create(size);
  }
  ~RasterPicture() {
    if (bitmap_) {
      delete bitmap_;
    }
    bitmap_ = nullptr;
  }

  Gdiplus::Bitmap * GetBitmap() {
    return bitmap_;
  }

  void create(const POINT& size) {
    delete bitmap_;
    bitmap_ = new Gdiplus::Bitmap{size.x, size.y, PixelFormat32bppRGB};
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

  void AddPoint(const Gdiplus::Point& point) {
    std::lock_guard<SpinLock> lock{ vector_lock_ };
    vector_.AddPoint(point);
  }

  void AddLine(const Gdiplus::Color& color) {
    std::lock_guard<SpinLock> lock{ vector_lock_ };
    vector_.AddLine(color);
  }

  void DrawLine(Gdiplus::Graphics& graphics, const std::size_t index) {
    auto &line{vector_.lines[index]};
    auto &points{vector_.points};
    if (line.length != 0) {
      Gdiplus::Pen pen(line.color, 4.f);
      if (line.length <= points.size() - line.start) {
        graphics.DrawLines(&pen, points.data() + line.start, line.length);
      }
      else {
        std::size_t right_part{ points.size() - line.start };
        graphics.DrawLines(&pen, points.data() + line.start, right_part);
        graphics.DrawLine(&pen, points.back(), points.front());
        graphics.DrawLines(&pen, points.data(), line.length - right_part);
      }
    }
  }

  void DrawVectorPicture(Gdiplus::Graphics& graphics) {
    graphics.Clear(Gdiplus::Color(255, 255, 255, 255));
    //graphics.SetSmoothingMode(Gdiplus::SmoothingMode::SmoothingModeNone);
    //graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
    //graphics.Flush(Gdiplus::FlushIntention::FlushIntentionSync);

    std::lock_guard<SpinLock> lock{vector_lock_};
    if (not vector_.lines.empty()) {
      auto max_line{(std::max)(vector_.end_line, vector_.lines.size() - 1)};
      for (std::size_t i{vector_.start_line}; i <= max_line; ++i) {
        DrawLine(graphics, i);  
      }

      if (vector_.end_line < vector_.start_line) {
        for (std::size_t i{}; i <= vector_.end_line; ++i) {
          DrawLine(graphics, i);
        }
      }
    }
  }

  void DrawRasterPicture(Gdiplus::Graphics& graphics) {
    std::lock_guard<SpinLock> lock{raster_lock_};
    auto * bitmap{raster_.GetBitmap()};
    if (bitmap) {
      Gdiplus::Rect size{0, 0, (INT)bitmap->GetWidth(), (INT)bitmap->GetHeight()};
      graphics.DrawImage(bitmap, size, 0, 0,
        (int)bitmap->GetWidth(),
        (int)bitmap->GetHeight(),
        Gdiplus::UnitPixel);
    }
  }

  void Rasterize() {
    std::lock_guard<SpinLock> lock{ raster_lock_ };
    auto * bitmap{raster_.GetBitmap()};
    if (bitmap) {
      Gdiplus::Graphics graphics{bitmap};
      DrawVectorPicture(graphics);
    }
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
