#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <map>

namespace Fio {

// ===================== 基础类型 =====================

struct ParsedPoint2D { double x = 0, y = 0; };
struct ParsedPoint3D { double x = 0, y = 0, z = 0; };

// ===================== 图元类型枚举 =====================

enum class ParsedGeometryType {
    Line, Arc, Circle, Ellipse, Polygon,
    Bezier, Bezier2, Nurbs, Spline,
    Polyline, Point, Text, Image,
    BarCode, QRCode, SmartLine,
    Mesh3D,
    Unknown
};

// ===================== 图层信息 =====================

struct ParsedLayer {
    uint32_t sourceId = 0;
    std::string name;
    uint32_t color = 0xFF000000;
    bool visible = true;
    bool locked = false;
};

// ===================== 群组信息 =====================

struct ParsedGroup {
    uint64_t sourceId = 0;
    uint64_t parentGroupSourceId = 0;
    std::string name;
    std::vector<uint64_t> entitySourceIds;
    std::vector<uint64_t> subGroupSourceIds;
};

// ===================== 图元数据 =====================

struct ParsedGeometry {
    uint64_t sourceId = 0;
    ParsedGeometryType type = ParsedGeometryType::Unknown;
    std::string name;
    uint32_t layerSourceId = 0;
    double lineWidth = 1.0;
    bool visible = true;
    bool locked = false;
    bool closed = false;
    bool ccw = true;

    struct LineData {
        ParsedPoint2D start, end;
    } line;

    struct ArcData {
        ParsedPoint2D center;
        double radius = 0;
        double startAngle = 0, endAngle = 0;
    } arc;

    struct CircleData {
        ParsedPoint2D center;
        double radius = 0;
    } circle;

    struct EllipseData {
        ParsedPoint2D center;
        double radiusX = 0, radiusY = 0;
        double rotation = 0;
        double startAngle = 0, endAngle = 0;
    } ellipse;

    struct PolylineData {
        std::vector<ParsedPoint2D> points;
        bool closed = false;
    } polyline;

    struct BezierData {
        ParsedPoint2D start;    // basePoint (P0)
        ParsedPoint2D ctrl0;    // ptCtrl0 (P1)
        ParsedPoint2D ctrl1;    // ptCtrl1 (P2)
        ParsedPoint2D end;      // ptEnd (P3)
    } bezier;

    struct Bezier2Data {
        ParsedPoint2D start;    // basePoint (P0)
        ParsedPoint2D ctrl;     // ptCtrl (P1)
        ParsedPoint2D end;      // ptEnd (P2)
    } bezier2;

    struct NurbsData {
        int degree = 3;
        std::vector<double> knots;
        std::vector<double> weights;
        std::vector<ParsedPoint2D> controlPoints;
    } nurbs;

    struct TextData {
        ParsedPoint2D position;
        std::string text;
        double height = 10.0;
        std::string fontFamily;
        double angle = 0.0;
    } text;

    struct ImageData {
        ParsedPoint2D position;
        int width = 0, height = 0;
        std::vector<uint8_t> data;
        double dpiX = 96.0, dpiY = 96.0;
    } image;

    struct SmartLineData {
        std::vector<ParsedGeometry> subEntities;
    } smartLine;

    struct MeshData {
        std::vector<ParsedPoint3D> vertices;
        std::vector<uint32_t> indices;
        std::vector<ParsedPoint3D> normals;
    } mesh;
};

// ===================== 文档元信息 =====================

struct ParsedMetadata {
    std::string author;
    std::string version;
    std::string creationDate;
    std::string modificationDate;
    std::string description;
    std::map<std::string, std::string> customProperties;
};

// ===================== 完整解析结果 =====================

struct ParseData {
    ParsedMetadata metadata;
    std::vector<ParsedLayer> layers;
    std::vector<ParsedGeometry> geometries;
    std::vector<ParsedGroup> groups;

    bool success = false;
    std::string errorMessage;
    std::vector<std::string> warnings;
};

} // namespace Fio