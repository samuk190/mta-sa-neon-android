// Desktop MTA Lua OOP class layer (CLuaVector2Defs, CLuaVector3Defs,
// CLuaVector4Defs, CLuaMatrixDefs, CLuaClassDefs, and CLua*Defs::AddClass).
// Compatible with both Lua 5.1 and Lua 5.3 VMs.

#include "LuaOopApi.h"
#include "LuaVMStub.h"

#include "lua.hpp"

namespace WorldRPG::Script {
namespace {

constexpr const char* kElementMt = "worldrpg.oop.element";
constexpr const char* kResourceMt = "worldrpg.oop.resource";

// Lua 5.1 & 5.3 portable OOP bootstrap. Installed once per VM after all
// procedural API functions have been registered.
// Parameters passed into the chunk: (elementMt, resourceMt).
static const char kOopBootstrapLua[] = R"LUA(
local elementMt, resourceMt = ...
local unpack = table.unpack or unpack
local G = _G
local rawget, rawset, type, tonumber, tostring = rawget, rawset, type, tonumber, tostring
local setmetatable, getmetatable, select, pairs = setmetatable, getmetatable, select, pairs
local math_sqrt, math_sin, math_cos, math_atan2, math_asin, math_deg, math_rad, math_abs =
    math.sqrt, math.sin, math.cos, math.atan2 or math.atan, math.asin, math.deg, math.rad, math.abs

local function isVec(v, tag)
    return type(v) == "table" and rawget(v, "__oopType") == tag
end

local function isMatrix(v)
    return type(v) == "table" and rawget(v, "__oopType") == "Matrix"
end

-- Unpack Vector2/3/4 or Matrix arguments into flat numeric/table arguments for procedural APIs.
local function unpackOopArgs(...)
    local n = select("#", ...)
    if n == 0 then return end
    local out = {}
    local k = 0
    for i = 1, n do
        local a = select(i, ...)
        if type(a) == "table" then
            local tag = rawget(a, "__oopType")
            if tag == "Vector3" then
                out[k + 1], out[k + 2], out[k + 3] = a._x, a._y, a._z
                k = k + 3
            elseif tag == "Vector2" then
                out[k + 1], out[k + 2] = a._x, a._y
                k = k + 2
            elseif tag == "Vector4" then
                out[k + 1], out[k + 2], out[k + 3], out[k + 4] = a._x, a._y, a._z, a._w
                k = k + 4
            elseif tag == "Matrix" then
                out[k + 1] = a:_toTable4x4()
                k = k + 1
            else
                k = k + 1
                out[k] = a
            end
        else
            k = k + 1
            out[k] = a
        end
    end
    return unpack(out, 1, k)
end

-- =========================================================================
-- Vector2 (CLuaVector2Defs.cpp)
-- =========================================================================
local Vector2 = {}
local Vector2MT = {}

local function newVector2(x, y)
    if isVec(x, "Vector2") then
        return setmetatable({ __oopType = "Vector2", _x = x._x, _y = x._y }, Vector2MT)
    elseif type(x) == "table" and x[1] ~= nil then
        return setmetatable({ __oopType = "Vector2", _x = tonumber(x[1]) or 0, _y = tonumber(x[2]) or 0 }, Vector2MT)
    end
    return setmetatable({ __oopType = "Vector2", _x = tonumber(x) or 0, _y = tonumber(y) or 0 }, Vector2MT)
end

function Vector2.create(x, y) return newVector2(x, y) end
function Vector2:getX() return self._x end
function Vector2:getY() return self._y end
function Vector2:setX(v) self._x = tonumber(v) or 0; return true end
function Vector2:setY(v) self._y = tonumber(v) or 0; return true end
function Vector2:getSquaredLength() return self._x * self._x + self._y * self._y end
function Vector2:getLength() return math_sqrt(self._x * self._x + self._y * self._y) end
function Vector2:normalize()
    local len = self:getLength()
    if len > 0 then self._x = self._x / len; self._y = self._y / len end
    return true
end
function Vector2:getNormalized()
    local len = self:getLength()
    if len > 0 then return newVector2(self._x / len, self._y / len) end
    return newVector2(0, 0)
end
function Vector2:dot(o)
    return self._x * o._x + self._y * o._y
end

Vector2MT.__index = function(self, k)
    if k == "x" then return self._x
    elseif k == "y" then return self._y
    elseif k == "length" then return self:getLength()
    elseif k == "squaredLength" then return self:getSquaredLength()
    elseif k == "normalized" then return self:getNormalized()
    end
    return Vector2[k]
end
Vector2MT.__newindex = function(self, k, v)
    if k == "x" then self._x = tonumber(v) or 0
    elseif k == "y" then self._y = tonumber(v) or 0
    else rawset(self, k, v)
    end
end
Vector2MT.__add = function(a, b) return newVector2(a._x + b._x, a._y + b._y) end
Vector2MT.__sub = function(a, b) return newVector2(a._x - b._x, a._y - b._y) end
Vector2MT.__unm = function(a) return newVector2(-a._x, -a._y) end
Vector2MT.__mul = function(a, b)
    if type(a) == "number" then return newVector2(a * b._x, a * b._y)
    elseif type(b) == "number" then return newVector2(a._x * b, a._y * b)
    else return newVector2(a._x * b._x, a._y * b._y)
    end
end
Vector2MT.__div = function(a, b)
    if type(b) == "number" then return newVector2(a._x / b, a._y / b)
    else return newVector2(a._x / b._x, a._y / b._y)
    end
end
Vector2MT.__eq = function(a, b)
    return isVec(a, "Vector2") and isVec(b, "Vector2") and a._x == b._x and a._y == b._y
end
Vector2MT.__tostring = function(a) return ("Vector2(%.4f, %.4f)"):format(a._x, a._y) end
setmetatable(Vector2, { __call = function(_, ...) return newVector2(...) end })

-- =========================================================================
-- Vector3 (CLuaVector3Defs.cpp)
-- =========================================================================
local Vector3 = {}
local Vector3MT = {}

local function newVector3(x, y, z)
    if isVec(x, "Vector3") then
        return setmetatable({ __oopType = "Vector3", _x = x._x, _y = x._y, _z = x._z }, Vector3MT)
    elseif type(x) == "table" and x[1] ~= nil then
        return setmetatable({ __oopType = "Vector3", _x = tonumber(x[1]) or 0, _y = tonumber(x[2]) or 0, _z = tonumber(x[3]) or 0 }, Vector3MT)
    end
    return setmetatable({ __oopType = "Vector3", _x = tonumber(x) or 0, _y = tonumber(y) or 0, _z = tonumber(z) or 0 }, Vector3MT)
end

function Vector3.create(x, y, z) return newVector3(x, y, z) end
function Vector3:getX() return self._x end
function Vector3:getY() return self._y end
function Vector3:getZ() return self._z end
function Vector3:setX(v) self._x = tonumber(v) or 0; return true end
function Vector3:setY(v) self._y = tonumber(v) or 0; return true end
function Vector3:setZ(v) self._z = tonumber(v) or 0; return true end
function Vector3:getSquaredLength() return self._x * self._x + self._y * self._y + self._z * self._z end
function Vector3:getLength() return math_sqrt(self._x * self._x + self._y * self._y + self._z * self._z) end
function Vector3:normalize()
    local len = self:getLength()
    if len > 0 then self._x = self._x / len; self._y = self._y / len; self._z = self._z / len end
    return true
end
function Vector3:getNormalized()
    local len = self:getLength()
    if len > 0 then return newVector3(self._x / len, self._y / len, self._z / len) end
    return newVector3(0, 0, 0)
end
function Vector3:dot(o)
    return self._x * o._x + self._y * o._y + self._z * o._z
end
function Vector3:cross(o)
    return newVector3(
        self._y * o._z - self._z * o._y,
        self._z * o._x - self._x * o._z,
        self._x * o._y - self._y * o._x
    )
end
function Vector3.intersectsSegmentTriangle(s0, s1, v0, v1, v2)
    local dir = s1 - s0
    local e1 = v1 - v0
    local e2 = v2 - v0
    local h = dir:cross(e2)
    local a = e1:dot(h)
    if math_abs(a) < 1e-7 then return false end
    local f = 1.0 / a
    local s = s0 - v0
    local u = f * s:dot(h)
    if u < 0.0 or u > 1.0 then return false end
    local q = s:cross(e1)
    local v = f * dir:dot(q)
    if v < 0.0 or u + v > 1.0 then return false end
    local t = f * e2:dot(q)
    return t >= 0.0 and t <= 1.0
end

Vector3MT.__index = function(self, k)
    if k == "x" then return self._x
    elseif k == "y" then return self._y
    elseif k == "z" then return self._z
    elseif k == "length" then return self:getLength()
    elseif k == "squaredLength" then return self:getSquaredLength()
    elseif k == "normalized" then return self:getNormalized()
    end
    return Vector3[k]
end
Vector3MT.__newindex = function(self, k, v)
    if k == "x" then self._x = tonumber(v) or 0
    elseif k == "y" then self._y = tonumber(v) or 0
    elseif k == "z" then self._z = tonumber(v) or 0
    else rawset(self, k, v)
    end
end
Vector3MT.__add = function(a, b) return newVector3(a._x + b._x, a._y + b._y, a._z + b._z) end
Vector3MT.__sub = function(a, b) return newVector3(a._x - b._x, a._y - b._y, a._z - b._z) end
Vector3MT.__unm = function(a) return newVector3(-a._x, -a._y, -a._z) end
Vector3MT.__mul = function(a, b)
    if type(a) == "number" then return newVector3(a * b._x, a * b._y, a * b._z)
    elseif type(b) == "number" then return newVector3(a._x * b, a._y * b, a._z * b)
    elseif isMatrix(b) then return b:transformPosition(a)
    else return newVector3(a._x * b._x, a._y * b._y, a._z * b._z)
    end
end
Vector3MT.__div = function(a, b)
    if type(b) == "number" then return newVector3(a._x / b, a._y / b, a._z / b)
    else return newVector3(a._x / b._x, a._y / b._y, a._z / b._z)
    end
end
Vector3MT.__eq = function(a, b)
    return isVec(a, "Vector3") and isVec(b, "Vector3") and a._x == b._x and a._y == b._y and a._z == b._z
end
Vector3MT.__tostring = function(a) return ("Vector3(%.4f, %.4f, %.4f)"):format(a._x, a._y, a._z) end
setmetatable(Vector3, { __call = function(_, ...) return newVector3(...) end })

-- =========================================================================
-- Vector4 (CLuaVector4Defs.cpp)
-- =========================================================================
local Vector4 = {}
local Vector4MT = {}

local function newVector4(x, y, z, w)
    if isVec(x, "Vector4") then
        return setmetatable({ __oopType = "Vector4", _x = x._x, _y = x._y, _z = x._z, _w = x._w }, Vector4MT)
    elseif type(x) == "table" and x[1] ~= nil then
        return setmetatable({ __oopType = "Vector4", _x = tonumber(x[1]) or 0, _y = tonumber(x[2]) or 0, _z = tonumber(x[3]) or 0, _w = tonumber(x[4]) or 0 }, Vector4MT)
    end
    return setmetatable({ __oopType = "Vector4", _x = tonumber(x) or 0, _y = tonumber(y) or 0, _z = tonumber(z) or 0, _w = tonumber(w) or 0 }, Vector4MT)
end

function Vector4.create(x, y, z, w) return newVector4(x, y, z, w) end
function Vector4:getX() return self._x end
function Vector4:getY() return self._y end
function Vector4:getZ() return self._z end
function Vector4:getW() return self._w end
function Vector4:setX(v) self._x = tonumber(v) or 0; return true end
function Vector4:setY(v) self._y = tonumber(v) or 0; return true end
function Vector4:setZ(v) self._z = tonumber(v) or 0; return true end
function Vector4:setW(v) self._w = tonumber(v) or 0; return true end
function Vector4:getSquaredLength() return self._x * self._x + self._y * self._y + self._z * self._z + self._w * self._w end
function Vector4:getLength() return math_sqrt(self:getSquaredLength()) end
function Vector4:normalize()
    local len = self:getLength()
    if len > 0 then self._x = self._x / len; self._y = self._y / len; self._z = self._z / len; self._w = self._w / len end
    return true
end
function Vector4:getNormalized()
    local len = self:getLength()
    if len > 0 then return newVector4(self._x / len, self._y / len, self._z / len, self._w / len) end
    return newVector4(0, 0, 0, 0)
end
function Vector4:dot(o)
    return self._x * o._x + self._y * o._y + self._z * o._z + self._w * o._w
end

Vector4MT.__index = function(self, k)
    if k == "x" then return self._x
    elseif k == "y" then return self._y
    elseif k == "z" then return self._z
    elseif k == "w" then return self._w
    elseif k == "length" then return self:getLength()
    elseif k == "squaredLength" then return self:getSquaredLength()
    elseif k == "normalized" then return self:getNormalized()
    end
    return Vector4[k]
end
Vector4MT.__newindex = function(self, k, v)
    if k == "x" then self._x = tonumber(v) or 0
    elseif k == "y" then self._y = tonumber(v) or 0
    elseif k == "z" then self._z = tonumber(v) or 0
    elseif k == "w" then self._w = tonumber(v) or 0
    else rawset(self, k, v)
    end
end
Vector4MT.__add = function(a, b) return newVector4(a._x + b._x, a._y + b._y, a._z + b._z, a._w + b._w) end
Vector4MT.__sub = function(a, b) return newVector4(a._x - b._x, a._y - b._y, a._z - b._z, a._w - b._w) end
Vector4MT.__unm = function(a) return newVector4(-a._x, -a._y, -a._z, -a._w) end
Vector4MT.__mul = function(a, b)
    if type(a) == "number" then return newVector4(a * b._x, a * b._y, a * b._z, a * b._w)
    elseif type(b) == "number" then return newVector4(a._x * b, a._y * b, a._z * b, a._w * b)
    else return newVector4(a._x * b._x, a._y * b._y, a._z * b._z, a._w * b._w)
    end
end
Vector4MT.__div = function(a, b)
    if type(b) == "number" then return newVector4(a._x / b, a._y / b, a._z / b, a._w / b)
    else return newVector4(a._x / b._x, a._y / b._y, a._z / b._z, a._w / b._w)
    end
end
Vector4MT.__eq = function(a, b)
    return isVec(a, "Vector4") and isVec(b, "Vector4") and a._x == b._x and a._y == b._y and a._z == b._z and a._w == b._w
end
Vector4MT.__tostring = function(a) return ("Vector4(%.4f, %.4f, %.4f, %.4f)"):format(a._x, a._y, a._z, a._w) end
setmetatable(Vector4, { __call = function(_, ...) return newVector4(...) end })

-- =========================================================================
-- Matrix (CLuaMatrixDefs.cpp + CMatrix.h)
-- Rows: right (r), forward (f), up (u), position (p).
-- =========================================================================
local Matrix = {}
local MatrixMT = {}

local function setRotationDeg(m, rx, ry, rz)
    local radX, radY, radZ = math_rad(rx or 0), math_rad(ry or 0), math_rad(rz or 0)
    local sx, cx = math_sin(radX), math_cos(radX)
    local sy, cy = math_sin(radY), math_cos(radY)
    local sz, cz = math_sin(radZ), math_cos(radZ)
    -- Desktop CMatrix::SetRotation (ZXY GTA convention)
    m._r = newVector3(cz * cy - sz * sx * sy, cy * sz + cz * sx * sy, -cx * sy)
    m._f = newVector3(-cx * sz, cz * cx, sx)
    m._u = newVector3(cz * sy + cy * sz * sx, sz * sy - cz * cy * sx, cx * cy)
end

local function newMatrix(a, b)
    local m = setmetatable({
        __oopType = "Matrix",
        _r = newVector3(1, 0, 0),
        _f = newVector3(0, 1, 0),
        _u = newVector3(0, 0, 1),
        _p = newVector3(0, 0, 0),
    }, MatrixMT)
    if isMatrix(a) then
        m._r = newVector3(a._r)
        m._f = newVector3(a._f)
        m._u = newVector3(a._u)
        m._p = newVector3(a._p)
        return m
    end
    if type(a) == "table" and type(a[1]) == "table" and type(a[2]) == "table" and type(a[3]) == "table" and type(a[4]) == "table" then
        m._r = newVector3(a[1][1], a[1][2], a[1][3])
        m._f = newVector3(a[2][1], a[2][2], a[2][3])
        m._u = newVector3(a[3][1], a[3][2], a[3][3])
        m._p = newVector3(a[4][1], a[4][2], a[4][3])
        return m
    end
    if a ~= nil then
        m._p = newVector3(a)
    end
    if b ~= nil then
        local rot = newVector3(b)
        setRotationDeg(m, rot._x, rot._y, rot._z)
    end
    return m
end

function Matrix.create(a, b) return newMatrix(a, b) end
function Matrix:_toTable4x4()
    return {
        { self._r._x, self._r._y, self._r._z, 0 },
        { self._f._x, self._f._y, self._f._z, 0 },
        { self._u._x, self._u._y, self._u._z, 0 },
        { self._p._x, self._p._y, self._p._z, 1 },
    }
end
function Matrix:getPosition() return newVector3(self._p) end
function Matrix:setPosition(v, y, z)
    self._p = isVec(v, "Vector3") and newVector3(v) or newVector3(v, y, z)
    return true
end
function Matrix:getRight() return newVector3(self._r) end
function Matrix:setRight(v, y, z)
    self._r = isVec(v, "Vector3") and newVector3(v) or newVector3(v, y, z)
    return true
end
function Matrix:getForward() return newVector3(self._f) end
function Matrix:setForward(v, y, z)
    self._f = isVec(v, "Vector3") and newVector3(v) or newVector3(v, y, z)
    return true
end
function Matrix:getUp() return newVector3(self._u) end
function Matrix:setUp(v, y, z)
    self._u = isVec(v, "Vector3") and newVector3(v) or newVector3(v, y, z)
    return true
end
function Matrix:getRotation()
    local fz = self._f._z
    if fz > 1 then fz = 1 elseif fz < -1 then fz = -1 end
    local rx = math_deg(math_asin(fz))
    local ry = math_deg(-math_atan2(self._r._z, self._u._z))
    local rz = math_deg(-math_atan2(self._f._x, self._f._y))
    return newVector3(rx, ry, rz)
end
function Matrix:setRotation(v, y, z)
    local rot = isVec(v, "Vector3") and v or newVector3(v, y, z)
    setRotationDeg(self, rot._x, rot._y, rot._z)
    return true
end
function Matrix:transformPosition(x, y, z)
    local vx, vy, vz
    if isVec(x, "Vector3") then vx, vy, vz = x._x, x._y, x._z
    else vx, vy, vz = tonumber(x) or 0, tonumber(y) or 0, tonumber(z) or 0
    end
    return newVector3(
        vx * self._r._x + vy * self._f._x + vz * self._u._x + self._p._x,
        vx * self._r._y + vy * self._f._y + vz * self._u._y + self._p._y,
        vx * self._r._z + vy * self._f._z + vz * self._u._z + self._p._z
    )
end
function Matrix:transformDirection(x, y, z)
    local vx, vy, vz
    if isVec(x, "Vector3") then vx, vy, vz = x._x, x._y, x._z
    else vx, vy, vz = tonumber(x) or 0, tonumber(y) or 0, tonumber(z) or 0
    end
    return newVector3(
        vx * self._r._x + vy * self._f._x + vz * self._u._x,
        vx * self._r._y + vy * self._f._y + vz * self._u._y,
        vx * self._r._z + vy * self._f._z + vz * self._u._z
    )
end
function Matrix:inverse()
    local r, f, u, p = self._r, self._f, self._u, self._p
    local inv = newMatrix()
    inv._r = newVector3(r._x, f._x, u._x)
    inv._f = newVector3(r._y, f._y, u._y)
    inv._u = newVector3(r._z, f._z, u._z)
    inv._p = newVector3(-p:dot(r), -p:dot(f), -p:dot(u))
    return inv
end

MatrixMT.__index = function(self, k)
    if k == "position" then return self:getPosition()
    elseif k == "rotation" then return self:getRotation()
    elseif k == "right" then return self:getRight()
    elseif k == "forward" then return self:getForward()
    elseif k == "up" then return self:getUp()
    end
    return Matrix[k]
end
MatrixMT.__newindex = function(self, k, v)
    if k == "position" then self:setPosition(v)
    elseif k == "rotation" then self:setRotation(v)
    elseif k == "right" then self:setRight(v)
    elseif k == "forward" then self:setForward(v)
    elseif k == "up" then self:setUp(v)
    else rawset(self, k, v)
    end
end
MatrixMT.__add = function(a, b)
    local m = newMatrix()
    m._r = a._r + b._r; m._f = a._f + b._f; m._u = a._u + b._u; m._p = a._p + b._p
    return m
end
MatrixMT.__sub = function(a, b)
    local m = newMatrix()
    m._r = a._r - b._r; m._f = a._f - b._f; m._u = a._u - b._u; m._p = a._p - b._p
    return m
end
MatrixMT.__mul = function(a, b)
    if isVec(b, "Vector3") then
        return a:transformPosition(b)
    end
    local m = newMatrix()
    m._r = b:transformDirection(a._r)
    m._f = b:transformDirection(a._f)
    m._u = b:transformDirection(a._u)
    m._p = b:transformPosition(a._p)
    return m
end
MatrixMT.__div = function(a, b) return a * b:inverse() end
MatrixMT.__eq = function(a, b)
    return isMatrix(a) and isMatrix(b) and a._r == b._r and a._f == b._f and a._u == b._u and a._p == b._p
end
MatrixMT.__tostring = function(m)
    return ("Matrix(%s, %s)"):format(tostring(m._p), tostring(m:getRotation()))
end
setmetatable(Matrix, { __call = function(_, ...) return newMatrix(...) end })

G.Vector2 = Vector2
G.Vector3 = Vector3
G.Vector4 = Vector4
G.Matrix = Matrix

-- =========================================================================
-- Class Registry & Inheritance Engine
-- =========================================================================
local classes = {}
local typeToClass = {}

local function fnWrap(globalName)
    return function(...)
        local fn = G[globalName]
        if type(fn) ~= "function" then return false end
        return fn(unpackOopArgs(...))
    end
end

local function vec3Getter(globalName)
    return function(self, ...)
        local fn = G[globalName]
        if type(fn) ~= "function" then return false end
        local x, y, z = fn(self, ...)
        if x == false or x == nil then return false end
        return newVector3(x, y, z)
    end
end

local function vec2Getter(globalName)
    return function(self, ...)
        local fn = G[globalName]
        if type(fn) ~= "function" then return false end
        local x, y = fn(self, ...)
        if x == false or x == nil then return false end
        return newVector2(x, y)
    end
end

local function defineClass(name, parentName, elementTypes)
    local cls = {
        __name = name,
        __parent = parentName and classes[parentName] or nil,
        __methods = {},
        __getters = {},
        __setters = {},
    }
    classes[name] = cls
    if elementTypes then
        for _, et in pairs(elementTypes) do
            typeToClass[et] = cls
        end
    end

    local clsMT = {
        __index = function(_, k)
            local cur = cls
            while cur do
                local g = rawget(cur, "__getters")[k]
                if g then return g() end
                local m = rawget(cur, "__methods")[k]
                if m then return m end
                cur = rawget(cur, "__parent")
            end
            return nil
        end,
        __newindex = function(_, k, v)
            local cur = cls
            while cur do
                local s = rawget(cur, "__setters")[k]
                if s then s(v); return end
                cur = rawget(cur, "__parent")
            end
            rawset(cls, k, v)
        end,
        __call = function(_, ...)
            local methods = rawget(cls, "__methods")
            local ctor = methods.create or methods.new
            if ctor then return ctor(...) end
            return false
        end,
    }
    setmetatable(cls, clsMT)
    G[name] = cls
    return cls
end

local function addMethods(cls, map)
    for k, v in pairs(map) do
        cls.__methods[k] = type(v) == "string" and fnWrap(v) or v
    end
end

local function addProps(cls, map)
    for k, spec in pairs(map) do
        local g, s = spec[1], spec[2]
        if g then cls.__getters[k] = type(g) == "string" and fnWrap(g) or g end
        if s then cls.__setters[k] = type(s) == "string" and fnWrap(s) or s end
    end
end

local function resolveElementClass(el)
    local getType = G.getElementType
    if type(getType) == "function" then
        local t = getType(el)
        if t and typeToClass[t] then return typeToClass[t] end
    end
    return classes.Element
end

-- Populate shared Element metatable (MT_ELEMENT)
elementMt.__index = function(self, k)
    local cur = resolveElementClass(self)
    while cur do
        local g = rawget(cur, "__getters")[k]
        if g then return g(self) end
        local m = rawget(cur, "__methods")[k]
        if m then return m end
        cur = rawget(cur, "__parent")
    end
    return nil
end

elementMt.__newindex = function(self, k, v)
    local cur = resolveElementClass(self)
    while cur do
        local s = rawget(cur, "__setters")[k]
        if s then s(self, v); return end
        cur = rawget(cur, "__parent")
    end
    rawset(self, k, v)
end

elementMt.__tostring = function(self)
    local getType = G.getElementType
    local t = type(getType) == "function" and getType(self) or "element"
    return ("Element(%s:%s)"):format(tostring(t), tostring(rawget(self, "id")))
end

-- =========================================================================
-- Element (CLuaElementDefs.cpp)
-- =========================================================================
local Element = defineClass("Element", nil, { "root", "resource", "map", "dummy", "console", "scriptfile" })
addMethods(Element, {
    create = "createElement",
    getByID = "getElementByID",
    getAllByType = "getElementsByType",
    getByType = "getElementsByType",
    destroy = "destroyElement",
    attach = "attachElements",
    detach = "detachElements",
    isAttached = "isElementAttached",
    getAttachedTo = "getElementAttachedTo",
    getAttachedElements = "getAttachedElements",
    getAttachedOffsets = "getElementAttachedOffsets",
    setAttachedOffsets = "setElementAttachedOffsets",
    getType = "getElementType",
    getID = "getElementID",
    setID = "setElementID",
    getParent = "getElementParent",
    setParent = "setElementParent",
    getChildren = "getElementChildren",
    getChild = "getElementChild",
    getChildrenCount = "getElementChildrenCount",
    getData = "getElementData",
    setData = "setElementData",
    hasData = "hasElementData",
    getAllData = "getAllElementData",
    getPosition = vec3Getter("getElementPosition"),
    setPosition = "setElementPosition",
    getRotation = vec3Getter("getElementRotation"),
    setRotation = "setElementRotation",
    getVelocity = vec3Getter("getElementVelocity"),
    setVelocity = "setElementVelocity",
    getAngularVelocity = vec3Getter("getElementAngularVelocity"),
    setAngularVelocity = "setElementAngularVelocity",
    getMatrix = function(self, ...)
        local fn = G.getElementMatrix
        if type(fn) ~= "function" then return false end
        local mat = fn(self, ...)
        if not mat then return false end
        return newMatrix(mat)
    end,
    setMatrix = "setElementMatrix",
    getInterior = "getElementInterior",
    setInterior = "setElementInterior",
    getDimension = "getElementDimension",
    setDimension = "setElementDimension",
    getAlpha = "getElementAlpha",
    setAlpha = "setElementAlpha",
    getHealth = "getElementHealth",
    setHealth = "setElementHealth",
    getModel = "getElementModel",
    setModel = "setElementModel",
    isFrozen = "isElementFrozen",
    setFrozen = "setElementFrozen",
    getCollisionsEnabled = "getElementCollisionsEnabled",
    setCollisionsEnabled = "setElementCollisionsEnabled",
    isCollidableWith = "isElementCollidableWith",
    setCollidableWith = "setElementCollidableWith",
    isDoubleSided = "isElementDoubleSided",
    setDoubleSided = "setElementDoubleSided",
    isStreamedIn = "isElementStreamedIn",
    isStreamable = "isElementStreamable",
    setStreamable = "setElementStreamable",
    isOnScreen = "isElementOnScreen",
    isInWater = "isElementInWater",
    isLocal = "isElementLocal",
    isSyncer = "isElementSyncer",
    isOnFire = "isElementOnFire",
    setOnFire = "setElementOnFire",
    isWithinColShape = "isElementWithinColShape",
    isWithinMarker = "isElementWithinMarker",
    getWithinColShape = "getElementsWithinColShape",
    getWithinRange = "getElementsWithinRange",
    getColShape = "getElementColShape",
    getBoundingBox = "getElementBoundingBox",
    getRadius = "getElementRadius",
    getLowLOD = "getLowLODElement",
    setLowLOD = "setLowLODElement",
    isLowLOD = "isElementLowLOD",
    isCallPropagationEnabled = "isElementCallPropagationEnabled",
    setCallPropagationEnabled = "setElementCallPropagationEnabled",
    getLighting = "getElementLighting",
    setLighting = "setElementLighting",
})
addProps(Element, {
    type = { "getElementType", nil },
    parent = { "getElementParent", "setElementParent" },
    children = { "getElementChildren", nil },
    childrenCount = { "getElementChildrenCount", nil },
    position = { vec3Getter("getElementPosition"), "setElementPosition" },
    rotation = { vec3Getter("getElementRotation"), "setElementRotation" },
    velocity = { vec3Getter("getElementVelocity"), "setElementVelocity" },
    angularVelocity = { vec3Getter("getElementAngularVelocity"), "setElementAngularVelocity" },
    matrix = { Element.__methods.getMatrix, "setElementMatrix" },
    interior = { "getElementInterior", "setElementInterior" },
    dimension = { "getElementDimension", "setElementDimension" },
    alpha = { "getElementAlpha", "setElementAlpha" },
    health = { "getElementHealth", "setElementHealth" },
    model = { "getElementModel", "setElementModel" },
    frozen = { "isElementFrozen", "setElementFrozen" },
    collisions = { "getElementCollisionsEnabled", "setElementCollisionsEnabled" },
    doubleSided = { "isElementDoubleSided", "setElementDoubleSided" },
    streamedIn = { "isElementStreamedIn", nil },
    streamable = { "isElementStreamable", "setElementStreamable" },
    onScreen = { "isElementOnScreen", nil },
    inWater = { "isElementInWater", nil },
    localElement = { "isElementLocal", nil },
    syncedByLocalPlayer = { "isElementSyncer", nil },
    onFire = { "isElementOnFire", "setElementOnFire" },
    attached = { "isElementAttached", nil },
    attachedTo = { "getElementAttachedTo", "attachElements" },
    colShape = { "getElementColShape", nil },
    radius = { "getElementRadius", nil },
    lowLOD = { "getLowLODElement", "setLowLODElement" },
    callPropagationEnabled = { "isElementCallPropagationEnabled", "setElementCallPropagationEnabled" },
    lighting = { "getElementLighting", "setElementLighting" },
    isElement = { "isElement", nil },
})

-- =========================================================================
-- Ped & Player (CLuaPedDefs.cpp, CLuaPlayerDefs.cpp)
-- =========================================================================
local Ped = defineClass("Ped", "Element", { "ped" })
addMethods(Ped, {
    create = "createPed",
    getArmor = "getPedArmor",
    setArmor = "setPedArmor",
    getWeapon = "getPedWeapon",
    getWeaponSlot = "getPedWeaponSlot",
    setWeaponSlot = "setPedWeaponSlot",
    getAmmoInClip = "getPedAmmoInClip",
    getTotalAmmo = "getPedTotalAmmo",
    giveWeapon = "givePedWeapon",
    getOccupiedVehicle = "getPedOccupiedVehicle",
    getOccupiedVehicleSeat = "getPedOccupiedVehicleSeat",
    isInVehicle = "isPedInVehicle",
    warpIntoVehicle = "warpPedIntoVehicle",
    removeFromVehicle = "removePedFromVehicle",
    isDead = "isPedDead",
    kill = "killPed",
    isDucked = "isPedDucked",
    isOnGround = "isPedOnGround",
    isOnFire = "isPedOnFire",
    setOnFire = "setPedOnFire",
    isHeadless = "isPedHeadless",
    setHeadless = "setPedHeadless",
    isChoking = "isPedChoking",
    setChoking = "setPedChoking",
    isDoingGangDriveby = "isPedDoingGangDriveby",
    setDoingGangDriveby = "setPedDoingGangDriveby",
    canBeKnockedOffBike = "canPedBeKnockedOffBike",
    setCanBeKnockedOffBike = "setPedCanBeKnockedOffBike",
    getOxygenLevel = "getPedOxygenLevel",
    setOxygenLevel = "setPedOxygenLevel",
    getStat = "getPedStat",
    setStat = "setPedStat",
    getWalkingStyle = "getPedWalkingStyle",
    setWalkingStyle = "setPedWalkingStyle",
    getVoice = "getPedVoice",
    setVoice = "setPedVoice",
    getAnimation = "getPedAnimation",
    setAnimation = "setPedAnimation",
    setAnimationProgress = "setPedAnimationProgress",
    setAnimationSpeed = "setPedAnimationSpeed",
    getControlState = "getPedControlState",
    setControlState = "setPedControlState",
    getAnalogControlState = "getPedAnalogControlState",
    setAnalogControlState = "setPedAnalogControlState",
    setAimTarget = "setPedAimTarget",
    setLookAt = "setPedLookAt",
    getCameraRotation = "getPedCameraRotation",
    setCameraRotation = "setPedCameraRotation",
    getTarget = "getPedTarget",
    getTargetStart = vec3Getter("getPedTargetStart"),
    getTargetEnd = vec3Getter("getPedTargetEnd"),
    getTargetCollision = vec3Getter("getPedTargetCollision"),
    getBonePosition = vec3Getter("getPedBonePosition"),
    getWeaponMuzzlePosition = vec3Getter("getPedWeaponMuzzlePosition"),
    getContactElement = "getPedContactElement",
    addClothes = "addPedClothes",
    removeClothes = "removePedClothes",
    getClothes = "getPedClothes",
    isWearingGoggles = "isPedWearingGoggles",
    setWearingGoggles = "setPedWearingGoggles",
})
addProps(Ped, {
    armor = { "getPedArmor", "setPedArmor" },
    weapon = { "getPedWeapon", nil },
    weaponSlot = { "getPedWeaponSlot", "setPedWeaponSlot" },
    vehicle = { "getPedOccupiedVehicle", "warpPedIntoVehicle" },
    occupiedVehicle = { "getPedOccupiedVehicle", nil },
    vehicleSeat = { "getPedOccupiedVehicleSeat", nil },
    occupiedVehicleSeat = { "getPedOccupiedVehicleSeat", nil },
    inVehicle = { "isPedInVehicle", nil },
    dead = { "isPedDead", nil },
    ducked = { "isPedDucked", nil },
    onGround = { "isPedOnGround", nil },
    headless = { "isPedHeadless", "setPedHeadless" },
    choking = { "isPedChoking", "setPedChoking" },
    oxygenLevel = { "getPedOxygenLevel", "setPedOxygenLevel" },
    walkingStyle = { "getPedWalkingStyle", "setPedWalkingStyle" },
    cameraRotation = { "getPedCameraRotation", "setPedCameraRotation" },
    target = { "getPedTarget", nil },
    contactElement = { "getPedContactElement", nil },
})

local Player = defineClass("Player", "Ped", { "player" })
addMethods(Player, {
    getFromName = "getPlayerFromName",
    getName = "getPlayerName",
    getPing = "getPlayerPing",
    getTeam = "getPlayerTeam",
    getMoney = "getPlayerMoney",
    getWantedLevel = "getPlayerWantedLevel",
    getNametagText = "getPlayerNametagText",
    setNametagText = "setPlayerNametagText",
    getNametagColor = "getPlayerNametagColor",
    setNametagColor = "setPlayerNametagColor",
    isNametagShowing = "isPlayerNametagShowing",
    setNametagShowing = "setPlayerNametagShowing",
    isMapForced = "isPlayerMapForced",
    isHudComponentVisible = "isPlayerHudComponentVisible",
    setHudComponentVisible = "setPlayerHudComponentVisible",
})
addProps(Player, {
    name = { "getPlayerName", nil },
    ping = { "getPlayerPing", nil },
    team = { "getPlayerTeam", nil },
    money = { "getPlayerMoney", nil },
    wantedLevel = { "getPlayerWantedLevel", nil },
    nametagText = { "getPlayerNametagText", "setPlayerNametagText" },
    nametagShowing = { "isPlayerNametagShowing", "setPlayerNametagShowing" },
})

-- =========================================================================
-- Vehicle (CLuaVehicleDefs.cpp)
-- =========================================================================
local Vehicle = defineClass("Vehicle", "Element", { "vehicle" })
addMethods(Vehicle, {
    create = "createVehicle",
    getModelFromName = "getVehicleModelFromName",
    getNameFromModel = "getVehicleNameFromModel",
    getName = "getVehicleName",
    getVehicleType = "getVehicleType",
    getColor = "getVehicleColor",
    setColor = "setVehicleColor",
    getHeadLightColor = "getVehicleHeadLightColor",
    setHeadLightColor = "setVehicleHeadLightColor",
    getOverrideLights = "getVehicleOverrideLights",
    setOverrideLights = "setVehicleOverrideLights",
    getEngineState = "getVehicleEngineState",
    setEngineState = "setVehicleEngineState",
    isLocked = "isVehicleLocked",
    setLocked = "setVehicleLocked",
    isDamageProof = "isVehicleDamageProof",
    setDamageProof = "setVehicleDamageProof",
    isDoorsUndamageable = "isVehicleDoorsUndamageable",
    setDoorsUndamageable = "setVehicleDoorsUndamageable",
    getLandingGearDown = "getVehicleLandingGearDown",
    setLandingGearDown = "setVehicleLandingGearDown",
    getSirensOn = "getVehicleSirensOn",
    setSirensOn = "setVehicleSirensOn",
    getTaxiLightOn = "isVehicleTaxiLightOn",
    setTaxiLightOn = "setVehicleTaxiLightOn",
    getPlateText = "getVehiclePlateText",
    setPlateText = "setVehiclePlateText",
    getPaintjob = "getVehiclePaintjob",
    setPaintjob = "setVehiclePaintjob",
    addUpgrade = "addVehicleUpgrade",
    removeUpgrade = "removeVehicleUpgrade",
    getUpgrades = "getVehicleUpgrades",
    getUpgradeOnSlot = "getVehicleUpgradeOnSlot",
    getCompatibleUpgrades = "getVehicleCompatibleUpgrades",
    getOccupant = "getVehicleOccupant",
    getOccupants = "getVehicleOccupants",
    getController = "getVehicleController",
    getMaxPassengers = "getVehicleMaxPassengers",
    fix = "fixVehicle",
    blow = "blowVehicle",
    isBlown = "isVehicleBlown",
    getDoorState = "getVehicleDoorState",
    setDoorState = "setVehicleDoorState",
    getLightState = "getVehicleLightState",
    setLightState = "setVehicleLightState",
    getPanelState = "getVehiclePanelState",
    setPanelState = "setVehiclePanelState",
    getWheelStates = "getVehicleWheelStates",
    setWheelStates = "setVehicleWheelStates",
    getDoorOpenRatio = "getVehicleDoorOpenRatio",
    setDoorOpenRatio = "setVehicleDoorOpenRatio",
    getHandling = "getVehicleHandling",
    setHandling = "setVehicleHandling",
    getComponentPosition = vec3Getter("getVehicleComponentPosition"),
    setComponentPosition = "setVehicleComponentPosition",
    getComponentRotation = vec3Getter("getVehicleComponentRotation"),
    setComponentRotation = "setVehicleComponentRotation",
    getComponentVisible = "getVehicleComponentVisible",
    setComponentVisible = "setVehicleComponentVisible",
    getComponents = "getVehicleComponents",
    getCurrentGear = "getVehicleCurrentGear",
    getNitroLevel = "getVehicleNitroLevel",
    setNitroLevel = "setVehicleNitroLevel",
    getNitroCount = "getVehicleNitroCount",
    setNitroCount = "setVehicleNitroCount",
    isNitroActivated = "isVehicleNitroActivated",
    setNitroActivated = "setVehicleNitroActivated",
    isNitroRecharging = "isVehicleNitroRecharging",
    attachTrailer = "attachTrailerToVehicle",
    detachTrailer = "detachTrailerFromVehicle",
    getTowingVehicle = "getVehicleTowingVehicle",
    getTowedByVehicle = "getVehicleTowedByVehicle",
    getTurretPosition = vec2Getter("getVehicleTurretPosition"),
    setTurretPosition = "setVehicleTurretPosition",
    isFuelTankExplodable = "isVehicleFuelTankExplodable",
    setFuelTankExplodable = "setVehicleFuelTankExplodable",
    isDerailed = "isTrainDerailed",
    setDerailed = "setTrainDerailed",
    isDerailable = "isTrainDerailable",
    setDerailable = "setTrainDerailable",
    getTrainDirection = "getTrainDirection",
    setTrainDirection = "setTrainDirection",
    getTrainSpeed = "getTrainSpeed",
    setTrainSpeed = "setTrainSpeed",
    getTrainPosition = "getTrainPosition",
    setTrainPosition = "setTrainPosition",
})
addProps(Vehicle, {
    name = { "getVehicleName", nil },
    vehicleType = { "getVehicleType", nil },
    engineState = { "getVehicleEngineState", "setVehicleEngineState" },
    locked = { "isVehicleLocked", "setVehicleLocked" },
    damageProof = { "isVehicleDamageProof", "setVehicleDamageProof" },
    overrideLights = { "getVehicleOverrideLights", "setVehicleOverrideLights" },
    landingGearDown = { "getVehicleLandingGearDown", "setVehicleLandingGearDown" },
    sirensOn = { "getVehicleSirensOn", "setVehicleSirensOn" },
    taxiLightOn = { "isVehicleTaxiLightOn", "setVehicleTaxiLightOn" },
    plateText = { "getVehiclePlateText", "setVehiclePlateText" },
    paintjob = { "getVehiclePaintjob", "setVehiclePaintjob" },
    occupants = { "getVehicleOccupants", nil },
    controller = { "getVehicleController", nil },
    maxPassengers = { "getVehicleMaxPassengers", nil },
    blown = { "isVehicleBlown", nil },
    nitroLevel = { "getVehicleNitroLevel", "setVehicleNitroLevel" },
    nitroCount = { "getVehicleNitroCount", "setVehicleNitroCount" },
    nitroActivated = { "isVehicleNitroActivated", "setVehicleNitroActivated" },
    towingVehicle = { "getVehicleTowingVehicle", nil },
    towedByVehicle = { "getVehicleTowedByVehicle", nil },
    derailed = { "isTrainDerailed", "setTrainDerailed" },
    derailable = { "isTrainDerailable", "setTrainDerailable" },
    trainDirection = { "getTrainDirection", "setTrainDirection" },
    trainSpeed = { "getTrainSpeed", "setTrainSpeed" },
    trainPosition = { "getTrainPosition", "setTrainPosition" },
})

-- =========================================================================
-- World / Gameplay Elements
-- =========================================================================
local Object = defineClass("Object", "Element", { "object" })
addMethods(Object, {
    create = "createObject",
    getScale = vec3Getter("getObjectScale"),
    setScale = "setObjectScale",
    getMass = "getObjectMass",
    setMass = "setObjectMass",
    isBreakable = "isObjectBreakable",
    setBreakable = "setObjectBreakable",
    move = "moveObject",
    stop = "stopObject",
    breakObject = "breakObject",
    respawn = "respawnObject",
})
addProps(Object, {
    scale = { vec3Getter("getObjectScale"), "setObjectScale" },
    mass = { "getObjectMass", "setObjectMass" },
    breakable = { "isObjectBreakable", "setObjectBreakable" },
})

local Marker = defineClass("Marker", "Element", { "marker" })
addMethods(Marker, {
    create = "createMarker",
    getCount = "getMarkerCount",
    getMarkerType = "getMarkerType",
    setMarkerType = "setMarkerType",
    getSize = "getMarkerSize",
    setSize = "setMarkerSize",
    getColor = "getMarkerColor",
    setColor = "setMarkerColor",
    getIcon = "getMarkerIcon",
    setIcon = "setMarkerIcon",
    getTarget = vec3Getter("getMarkerTarget"),
    setTarget = "setMarkerTarget",
})
addProps(Marker, {
    markerType = { "getMarkerType", "setMarkerType" },
    size = { "getMarkerSize", "setMarkerSize" },
    icon = { "getMarkerIcon", "setMarkerIcon" },
    target = { vec3Getter("getMarkerTarget"), "setMarkerTarget" },
})

local Blip = defineClass("Blip", "Element", { "blip" })
addMethods(Blip, {
    create = "createBlip",
    createAttachedTo = "createBlipAttachedTo",
    getIcon = "getBlipIcon",
    setIcon = "setBlipIcon",
    getSize = "getBlipSize",
    setSize = "setBlipSize",
    getColor = "getBlipColor",
    setColor = "setBlipColor",
    getOrdering = "getBlipOrdering",
    setOrdering = "setBlipOrdering",
    getVisibleDistance = "getBlipVisibleDistance",
    setVisibleDistance = "setBlipVisibleDistance",
})
addProps(Blip, {
    icon = { "getBlipIcon", "setBlipIcon" },
    size = { "getBlipSize", "setBlipSize" },
    ordering = { "getBlipOrdering", "setBlipOrdering" },
    visibleDistance = { "getBlipVisibleDistance", "setBlipVisibleDistance" },
})

local ColShape = defineClass("ColShape", "Element", { "colshape" })
addMethods(ColShape, {
    Circle = "createColCircle",
    Cuboid = "createColCuboid",
    Sphere = "createColSphere",
    Rectangle = "createColRectangle",
    Polygon = "createColPolygon",
    Tube = "createColTube",
    getShapeType = "getColShapeType",
    getRadius = "getColShapeRadius",
    setRadius = "setColShapeRadius",
    getSize = "getColShapeSize",
    setSize = "setColShapeSize",
    getElementsWithin = "getElementsWithinColShape",
    isInside = "isInsideColShape",
})
addProps(ColShape, {
    shapeType = { "getColShapeType", nil },
    radius = { "getColShapeRadius", "setColShapeRadius" },
    size = { "getColShapeSize", "setColShapeSize" },
    elementsWithin = { "getElementsWithinColShape", nil },
})

local Pickup = defineClass("Pickup", "Element", { "pickup" })
addMethods(Pickup, {
    create = "createPickup",
    getPickupType = "getPickupType",
    setPickupType = "setPickupType",
    getAmount = "getPickupAmount",
    getAmmo = "getPickupAmmo",
    getWeapon = "getPickupWeapon",
})
addProps(Pickup, {
    pickupType = { "getPickupType", "setPickupType" },
    amount = { "getPickupAmount", nil },
    ammo = { "getPickupAmmo", nil },
    weapon = { "getPickupWeapon", nil },
})

local Team = defineClass("Team", "Element", { "team" })
addMethods(Team, {
    getFromName = "getTeamFromName",
    getName = "getTeamName",
    getColor = "getTeamColor",
    getFriendlyFire = "getTeamFriendlyFire",
    getPlayers = "getPlayersInTeam",
    countPlayers = "countPlayersInTeam",
})
addProps(Team, {
    name = { "getTeamName", nil },
    friendlyFire = { "getTeamFriendlyFire", nil },
    players = { "getPlayersInTeam", nil },
    playerCount = { "countPlayersInTeam", nil },
})

local RadarArea = defineClass("RadarArea", "Element", { "radararea" })
addMethods(RadarArea, {
    create = "createRadarArea",
    getSize = vec2Getter("getRadarAreaSize"),
    setSize = "setRadarAreaSize",
    getColor = "getRadarAreaColor",
    setColor = "setRadarAreaColor",
    isFlashing = "isRadarAreaFlashing",
    setFlashing = "setRadarAreaFlashing",
    isInside = "isInsideRadarArea",
})
addProps(RadarArea, {
    size = { vec2Getter("getRadarAreaSize"), "setRadarAreaSize" },
    flashing = { "isRadarAreaFlashing", "setRadarAreaFlashing" },
})

local Sound = defineClass("Sound", "Element", { "sound" })
addMethods(Sound, {
    create = "playSound",
    stop = "stopSound",
    getVolume = "getSoundVolume",
    setVolume = "setSoundVolume",
    getSpeed = "getSoundSpeed",
    setSpeed = "setSoundSpeed",
    isPaused = "isSoundPaused",
    setPaused = "setSoundPaused",
    getLength = "getSoundLength",
    getPlaybackPosition = "getSoundPosition",
    setPlaybackPosition = "setSoundPosition",
    getPan = "getSoundPan",
    setPan = "setSoundPan",
    getMetaTags = "getSoundMetaTags",
    getProperties = "getSoundProperties",
    setProperties = "setSoundProperties",
    setEffectEnabled = "setSoundEffectEnabled",
    getEffects = "getSoundEffects",
    getFFTData = "getSoundFFTData",
    getWaveData = "getSoundWaveData",
    getLevelData = "getSoundLevelData",
    getBPM = "getSoundBPM",
})
addProps(Sound, {
    volume = { "getSoundVolume", "setSoundVolume" },
    speed = { "getSoundSpeed", "setSoundSpeed" },
    paused = { "isSoundPaused", "setSoundPaused" },
    length = { "getSoundLength", nil },
    playbackPosition = { "getSoundPosition", "setSoundPosition" },
    pan = { "getSoundPan", "setSoundPan" },
    metaTags = { "getSoundMetaTags", nil },
})

local Sound3D = defineClass("Sound3D", "Sound", { "sound3d" })
addMethods(Sound3D, {
    create = "playSound3D",
    getMinDistance = "getSoundMinDistance",
    setMinDistance = "setSoundMinDistance",
    getMaxDistance = "getSoundMaxDistance",
    setMaxDistance = "setSoundMaxDistance",
})
addProps(Sound3D, {
    minDistance = { "getSoundMinDistance", "setSoundMinDistance" },
    maxDistance = { "getSoundMaxDistance", "setSoundMaxDistance" },
})

local Water = defineClass("Water", "Element", { "water" })
addMethods(Water, {
    create = "createWater",
    getLevel = "getWaterLevel",
    setLevel = "setWaterLevel",
    getVertexPosition = vec3Getter("getWaterVertexPosition"),
    setVertexPosition = "setWaterVertexPosition",
})
addProps(Water, {
    level = { "getWaterLevel", "setWaterLevel" },
})

local Weapon = defineClass("Weapon", "Element", { "weapon" })
addMethods(Weapon, {
    create = "createWeapon",
    fire = "fireWeapon",
    resetFiringRate = "resetWeaponFiringRate",
    getState = "getWeaponState",
    setState = "setWeaponState",
    getAmmo = "getWeaponAmmo",
    setAmmo = "setWeaponAmmo",
    getClipAmmo = "getWeaponClipAmmo",
    setClipAmmo = "setWeaponClipAmmo",
    getFiringRate = "getWeaponFiringRate",
    setFiringRate = "setWeaponFiringRate",
    getTarget = "getWeaponTarget",
    setTarget = "setWeaponTarget",
    getOwner = "getWeaponOwner",
    getFlags = "getWeaponFlags",
    setFlags = "setWeaponFlags",
    getProperty = "getWeaponProperty",
    setProperty = "setWeaponProperty",
})
addProps(Weapon, {
    state = { "getWeaponState", "setWeaponState" },
    ammo = { "getWeaponAmmo", "setWeaponAmmo" },
    clipAmmo = { "getWeaponClipAmmo", "setWeaponClipAmmo" },
    firingRate = { "getWeaponFiringRate", "setWeaponFiringRate" },
    target = { "getWeaponTarget", "setWeaponTarget" },
    owner = { "getWeaponOwner", nil },
})

local Projectile = defineClass("Projectile", "Element", { "projectile" })
addMethods(Projectile, {
    create = "createProjectile",
    getCreator = "getProjectileCreator",
    getTarget = "getProjectileTarget",
    getForce = "getProjectileForce",
    getCounter = "getProjectileCounter",
    setCounter = "setProjectileCounter",
})
addProps(Projectile, {
    creator = { "getProjectileCreator", nil },
    target = { "getProjectileTarget", nil },
    force = { "getProjectileForce", nil },
    counter = { "getProjectileCounter", "setProjectileCounter" },
})

local Effect = defineClass("Effect", "Element", { "effect" })
addMethods(Effect, {
    create = "createEffect",
    getSpeed = "getEffectSpeed",
    setSpeed = "setEffectSpeed",
    getDensity = "getEffectDensity",
    setDensity = "setEffectDensity",
})
addProps(Effect, {
    speed = { "getEffectSpeed", "setEffectSpeed" },
    density = { "getEffectDensity", "setEffectDensity" },
})

local Light = defineClass("Light", "Element", { "light" })
addMethods(Light, {
    create = "createLight",
    getLightType = "getLightType",
    getRadius = "getLightRadius",
    setRadius = "setLightRadius",
    getColor = "getLightColor",
    setColor = "setLightColor",
    getDirection = vec3Getter("getLightDirection"),
    setDirection = "setLightDirection",
})
addProps(Light, {
    lightType = { "getLightType", nil },
    radius = { "getLightRadius", "setLightRadius" },
    direction = { vec3Getter("getLightDirection"), "setLightDirection" },
})

local SearchLight = defineClass("SearchLight", "Element", { "searchlight" })
addMethods(SearchLight, {
    create = "createSearchLight",
    getStartPosition = vec3Getter("getSearchLightStartPosition"),
    setStartPosition = "setSearchLightStartPosition",
    getEndPosition = vec3Getter("getSearchLightEndPosition"),
    setEndPosition = "setSearchLightEndPosition",
    getStartRadius = "getSearchLightStartRadius",
    setStartRadius = "setSearchLightStartRadius",
    getEndRadius = "getSearchLightEndRadius",
    setEndRadius = "setSearchLightEndRadius",
})

local Building = defineClass("Building", "Element", { "building" })
addMethods(Building, { create = "createBuilding" })

local Bird = defineClass("Bird", "Element", { "bird" })
local Foliage = defineClass("Foliage", "Element", { "foliage" })
local BreakEffect = defineClass("BreakEffect", "Element", { "breakeffect" })
local TrainTrack = defineClass("TrainTrack", "Element", { "traintrack" })
addMethods(TrainTrack, { getDefault = "getDefaultTrack" })

-- =========================================================================
-- GUI Classes (CLuaGUIDefs.cpp)
-- =========================================================================
local GuiElement = defineClass("GuiElement", "Element", { "gui-element" })
addMethods(GuiElement, {
    bringToFront = "guiBringToFront",
    moveToBack = "guiMoveToBack",
    getPosition = vec2Getter("guiGetPosition"),
    setPosition = "guiSetPosition",
    getSize = vec2Getter("guiGetSize"),
    setSize = "guiSetSize",
    getText = "guiGetText",
    setText = "guiSetText",
    getVisible = "guiGetVisible",
    setVisible = "guiSetVisible",
    getEnabled = "guiGetEnabled",
    setEnabled = "guiSetEnabled",
    getAlpha = "guiGetAlpha",
    setAlpha = "guiSetAlpha",
    getFont = "guiGetFont",
    setFont = "guiSetFont",
    getProperty = "guiGetProperty",
    setProperty = "guiSetProperty",
    getProperties = "guiGetProperties",
    focus = "guiFocus",
    blur = "guiBlur",
})
addProps(GuiElement, {
    position = { vec2Getter("guiGetPosition"), "guiSetPosition" },
    size = { vec2Getter("guiGetSize"), "guiSetSize" },
    text = { "guiGetText", "guiSetText" },
    visible = { "guiGetVisible", "guiSetVisible" },
    enabled = { "guiGetEnabled", "guiSetEnabled" },
    alpha = { "guiGetAlpha", "guiSetAlpha" },
    font = { "guiGetFont", "guiSetFont" },
})

local GuiWindow = defineClass("GuiWindow", "GuiElement", { "gui-window" })
addMethods(GuiWindow, { create = "guiCreateWindow", isMovable = "guiWindowIsMovable", setMovable = "guiWindowSetMovable", isSizable = "guiWindowIsSizable", setSizable = "guiWindowSetSizable" })
addProps(GuiWindow, { movable = { "guiWindowIsMovable", "guiWindowSetMovable" }, sizable = { "guiWindowIsSizable", "guiWindowSetSizable" } })

local GuiButton = defineClass("GuiButton", "GuiElement", { "gui-button" })
addMethods(GuiButton, { create = "guiCreateButton" })

local GuiEdit = defineClass("GuiEdit", "GuiElement", { "gui-edit" })
addMethods(GuiEdit, { create = "guiCreateEdit", isMasked = "guiEditIsMasked", setMasked = "guiEditSetMasked", isReadOnly = "guiEditIsReadOnly", setReadOnly = "guiEditSetReadOnly", getMaxLength = "guiEditGetMaxLength", setMaxLength = "guiEditSetMaxLength", getCaretIndex = "guiEditGetCaretIndex", setCaretIndex = "guiEditSetCaretIndex" })
addProps(GuiEdit, { masked = { "guiEditIsMasked", "guiEditSetMasked" }, readOnly = { "guiEditIsReadOnly", "guiEditSetReadOnly" }, maxLength = { "guiEditGetMaxLength", "guiEditSetMaxLength" }, caretIndex = { "guiEditGetCaretIndex", "guiEditSetCaretIndex" } })

local GuiLabel = defineClass("GuiLabel", "GuiElement", { "gui-label" })
addMethods(GuiLabel, { create = "guiCreateLabel", getColor = "guiLabelGetColor", setColor = "guiLabelSetColor", setHorizontalAlign = "guiLabelSetHorizontalAlign", setVerticalAlign = "guiLabelSetVerticalAlign", getTextExtent = "guiLabelGetTextExtent", getFontHeight = "guiLabelGetFontHeight" })

local GuiMemo = defineClass("GuiMemo", "GuiElement", { "gui-memo" })
addMethods(GuiMemo, { create = "guiCreateMemo", isReadOnly = "guiMemoIsReadOnly", setReadOnly = "guiMemoSetReadOnly", getCaretIndex = "guiMemoGetCaretIndex", setCaretIndex = "guiMemoSetCaretIndex", getVerticalScrollPosition = "guiMemoGetVerticalScrollPosition", setVerticalScrollPosition = "guiMemoSetVerticalScrollPosition" })
addProps(GuiMemo, { readOnly = { "guiMemoIsReadOnly", "guiMemoSetReadOnly" }, caretIndex = { "guiMemoGetCaretIndex", "guiMemoSetCaretIndex" } })

local GuiStaticImage = defineClass("GuiStaticImage", "GuiElement", { "gui-staticimage" })
addMethods(GuiStaticImage, { create = "guiCreateStaticImage", loadImage = "guiStaticImageLoadImage", getNativeSize = vec2Getter("guiStaticImageGetNativeSize") })

local GuiComboBox = defineClass("GuiComboBox", "GuiElement", { "gui-combobox" })
addMethods(GuiComboBox, { create = "guiCreateComboBox", addItem = "guiComboBoxAddItem", removeItem = "guiComboBoxRemoveItem", clear = "guiComboBoxClear", getSelected = "guiComboBoxGetSelected", setSelected = "guiComboBoxSetSelected", getItemText = "guiComboBoxGetItemText", setItemText = "guiComboBoxSetItemText", getItemCount = "guiComboBoxGetItemCount", isOpen = "guiComboBoxIsOpen", setOpen = "guiComboBoxSetOpen" })
addProps(GuiComboBox, { selected = { "guiComboBoxGetSelected", "guiComboBoxSetSelected" }, open = { "guiComboBoxIsOpen", "guiComboBoxSetOpen" }, itemCount = { "guiComboBoxGetItemCount", nil } })

local GuiCheckBox = defineClass("GuiCheckBox", "GuiElement", { "gui-checkbox" })
addMethods(GuiCheckBox, { create = "guiCreateCheckBox", getSelected = "guiCheckBoxGetSelected", setSelected = "guiCheckBoxSetSelected" })
addProps(GuiCheckBox, { selected = { "guiCheckBoxGetSelected", "guiCheckBoxSetSelected" } })

local GuiRadioButton = defineClass("GuiRadioButton", "GuiElement", { "gui-radiobutton" })
addMethods(GuiRadioButton, { create = "guiCreateRadioButton", getSelected = "guiRadioButtonGetSelected", setSelected = "guiRadioButtonSetSelected" })
addProps(GuiRadioButton, { selected = { "guiRadioButtonGetSelected", "guiRadioButtonSetSelected" } })

local GuiScrollPane = defineClass("GuiScrollPane", "GuiElement", { "gui-scrollpane" })
addMethods(GuiScrollPane, { create = "guiCreateScrollPane", getHorizontalScrollPosition = "guiScrollPaneGetHorizontalScrollPosition", setHorizontalScrollPosition = "guiScrollPaneSetHorizontalScrollPosition", getVerticalScrollPosition = "guiScrollPaneGetVerticalScrollPosition", setVerticalScrollPosition = "guiScrollPaneSetVerticalScrollPosition", setScrollBars = "guiScrollPaneSetScrollBars" })

local GuiScrollBar = defineClass("GuiScrollBar", "GuiElement", { "gui-scrollbar" })
addMethods(GuiScrollBar, { create = "guiCreateScrollBar", getScrollPosition = "guiScrollBarGetScrollPosition", setScrollPosition = "guiScrollBarSetScrollPosition" })
addProps(GuiScrollBar, { scrollPosition = { "guiScrollBarGetScrollPosition", "guiScrollBarSetScrollPosition" } })

local GuiProgressBar = defineClass("GuiProgressBar", "GuiElement", { "gui-progressbar" })
addMethods(GuiProgressBar, { create = "guiCreateProgressBar", getProgress = "guiProgressBarGetProgress", setProgress = "guiProgressBarSetProgress" })
addProps(GuiProgressBar, { progress = { "guiProgressBarGetProgress", "guiProgressBarSetProgress" } })

local GuiGridList = defineClass("GuiGridList", "GuiElement", { "gui-gridlist" })
addMethods(GuiGridList, {
    create = "guiCreateGridList",
    addColumn = "guiGridListAddColumn",
    removeColumn = "guiGridListRemoveColumn",
    autoSizeColumn = "guiGridListAutoSizeColumn",
    getColumnCount = "guiGridListGetColumnCount",
    getColumnTitle = "guiGridListGetColumnTitle",
    setColumnTitle = "guiGridListSetColumnTitle",
    getColumnWidth = "guiGridListGetColumnWidth",
    setColumnWidth = "guiGridListSetColumnWidth",
    addRow = "guiGridListAddRow",
    insertRowAfter = "guiGridListInsertRowAfter",
    removeRow = "guiGridListRemoveRow",
    clear = "guiGridListClear",
    getRowCount = "guiGridListGetRowCount",
    getItemText = "guiGridListGetItemText",
    setItemText = "guiGridListSetItemText",
    getItemData = "guiGridListGetItemData",
    setItemData = "guiGridListSetItemData",
    getItemColor = "guiGridListGetItemColor",
    setItemColor = "guiGridListSetItemColor",
    getSelectedItem = "guiGridListGetSelectedItem",
    setSelectedItem = "guiGridListSetSelectedItem",
    getSelectedCount = "guiGridListGetSelectedCount",
    getSelectedItems = "guiGridListGetSelectedItems",
    getSelectionMode = "guiGridListGetSelectionMode",
    setSelectionMode = "guiGridListSetSelectionMode",
    isSortingEnabled = "guiGridListIsSortingEnabled",
    setSortingEnabled = "guiGridListSetSortingEnabled",
    setScrollBars = "guiGridListSetScrollBars",
    getHorizontalScrollPosition = "guiGridListGetHorizontalScrollPosition",
    setHorizontalScrollPosition = "guiGridListSetHorizontalScrollPosition",
    getVerticalScrollPosition = "guiGridListGetVerticalScrollPosition",
    setVerticalScrollPosition = "guiGridListSetVerticalScrollPosition",
})
addProps(GuiGridList, {
    columnCount = { "guiGridListGetColumnCount", nil },
    rowCount = { "guiGridListGetRowCount", nil },
    selectionMode = { "guiGridListGetSelectionMode", "guiGridListSetSelectionMode" },
    sortingEnabled = { "guiGridListIsSortingEnabled", "guiGridListSetSortingEnabled" },
    selectedCount = { "guiGridListGetSelectedCount", nil },
})

local GuiTabPanel = defineClass("GuiTabPanel", "GuiElement", { "gui-tabpanel" })
addMethods(GuiTabPanel, { create = "guiCreateTabPanel", getSelectedTab = "guiGetSelectedTab", setSelectedTab = "guiSetSelectedTab", deleteTab = "guiDeleteTab" })
addProps(GuiTabPanel, { selectedTab = { "guiGetSelectedTab", "guiSetSelectedTab" } })

local GuiTab = defineClass("GuiTab", "GuiElement", { "gui-tab" })
addMethods(GuiTab, { create = "guiCreateTab" })

local GuiBrowser = defineClass("GuiBrowser", "GuiElement", { "gui-browser" })
addMethods(GuiBrowser, { create = "guiCreateBrowser", getBrowser = "guiGetBrowser" })
addProps(GuiBrowser, { browser = { "guiGetBrowser", nil } })

local GuiFont = defineClass("GuiFont", "Element", { "gui-font" })
addMethods(GuiFont, { create = "guiCreateFont" })

-- =========================================================================
-- DX & Engine Asset Classes (CLuaDrawingDefs.cpp, CLuaEngineDefs.cpp)
-- =========================================================================
local DxMaterial = defineClass("DxMaterial", "Element", { "material" })
addMethods(DxMaterial, { getSize = vec2Getter("dxGetMaterialSize") })
addProps(DxMaterial, { size = { vec2Getter("dxGetMaterialSize"), nil } })

local DxTexture = defineClass("DxTexture", "DxMaterial", { "texture" })
addMethods(DxTexture, { create = "dxCreateTexture", getPixels = "dxGetTexturePixels", setPixels = "dxSetTexturePixels", setEdge = "dxSetTextureEdge" })

local DxScreenSource = defineClass("DxScreenSource", "DxTexture", { "screen-source" })
addMethods(DxScreenSource, { create = "dxCreateScreenSource", update = "dxUpdateScreenSource" })

local DxRenderTarget = defineClass("DxRenderTarget", "DxTexture", { "render-target" })
addMethods(DxRenderTarget, { create = "dxCreateRenderTarget", setAsTarget = "dxSetRenderTarget" })

local DxShader = defineClass("DxShader", "DxMaterial", { "shader" })
addMethods(DxShader, { create = "dxCreateShader", setValue = "dxSetShaderValue", setTessellation = "dxSetShaderTessellation", setTransform = "dxSetShaderTransform", applyToWorldTexture = "engineApplyShaderToWorldTexture", removeFromWorldTexture = "engineRemoveShaderFromWorldTexture" })

local DxFont = defineClass("DxFont", "Element", { "dx-font" })
addMethods(DxFont, { create = "dxCreateFont" })

local SVG = defineClass("SVG", "DxTexture", { "svg" })
addMethods(SVG, { create = "svgCreate", getSize = vec2Getter("svgGetSize"), setSize = "svgSetSize", getDocumentXML = "svgGetDocumentXML", setDocumentXML = "svgSetDocumentXML", setUpdateCallback = "svgSetUpdateCallback" })

local Browser = defineClass("Browser", "DxTexture", { "webbrowser" })
addMethods(Browser, { create = "createBrowser", loadURL = "loadBrowserURL", isLoading = "isBrowserLoading", getURL = "getBrowserURL", getTitle = "getBrowserTitle", executeJavascript = "executeBrowserJavascript", setVolume = "setBrowserVolume", focus = "focusBrowser", resize = "resizeBrowser", reloadPage = "reloadBrowserPage", navigateBack = "navigateBrowserBack", navigateForward = "navigateBrowserForward" })

local EngineTXD = defineClass("EngineTXD", "Element", { "txd" })
addMethods(EngineTXD, { create = "engineLoadTXD", import = "engineImportTXD" })

local EngineDFF = defineClass("EngineDFF", "Element", { "dff" })
addMethods(EngineDFF, { create = "engineLoadDFF", replace = "engineReplaceModel" })

local EngineCOL = defineClass("EngineCOL", "Element", { "col" })
addMethods(EngineCOL, { create = "engineLoadCOL", replace = "engineReplaceCOL" })

local EngineIFP = defineClass("EngineIFP", "Element", { "ifp" })
addMethods(EngineIFP, { create = "engineLoadIFP" })

local EngineIMG = defineClass("EngineIMG", "Element", { "img" })
addMethods(EngineIMG, { create = "engineLoadIMG", add = "engineAddImage", remove = "engineRemoveImage", getFilesCount = "engineImageGetFilesCount", getFiles = "engineImageGetFiles", getFile = "engineImageGetFile", linkDFF = "engineImageLinkDFF", linkTXD = "engineImageLinkTXD" })

-- =========================================================================
-- Static Classes: Camera, Engine, World, DiscordRPC, path
-- =========================================================================
local Camera = defineClass("Camera", nil, nil)
addMethods(Camera, {
    getMatrix = function()
        local x, y, z, lx, ly, lz = G.getCameraMatrix()
        if not x then return false end
        local fwd = (newVector3(lx - x, ly - y, lz - z)):getNormalized()
        local up = newVector3(0, 0, 1)
        local right = fwd:cross(up):getNormalized()
        if right:getLength() < 1e-4 then right = newVector3(1, 0, 0) end
        up = right:cross(fwd):getNormalized()
        local m = newMatrix()
        m._r = right; m._f = fwd; m._u = up; m._p = newVector3(x, y, z)
        return m
    end,
    setMatrix = function(a, b, ...)
        if isMatrix(a) then
            local p = a._p
            local l = a._p + a._f
            return G.setCameraMatrix(p._x, p._y, p._z, l._x, l._y, l._z)
        end
        return G.setCameraMatrix(unpackOopArgs(a, b, ...))
    end,
    getPosition = function()
        local x, y, z = G.getCameraMatrix()
        if not x then return false end
        return newVector3(x, y, z)
    end,
    setPosition = function(v, y, z)
        local p = isVec(v, "Vector3") and v or newVector3(v, y, z)
        local _, _, _, lx, ly, lz, roll, fov = G.getCameraMatrix()
        return G.setCameraMatrix(p._x, p._y, p._z, lx or p._x, ly or (p._y + 1), lz or p._z, roll or 0, fov or 70)
    end,
    getTarget = "getCameraTarget",
    setTarget = "setCameraTarget",
    getInterior = "getCameraInterior",
    setInterior = "setCameraInterior",
    fade = "fadeCamera",
    getGoggleEffect = "getCameraGoggleEffect",
    setGoggleEffect = "setCameraGoggleEffect",
    getViewMode = "getCameraViewMode",
    setViewMode = "setCameraViewMode",
    getClip = "getCameraClip",
    setClip = "setCameraClip",
    getFieldOfView = "getCameraFieldOfView",
    setFieldOfView = "setCameraFieldOfView",
    getShakeLevel = "getCameraShakeLevel",
    setShakeLevel = "setCameraShakeLevel",
})
addProps(Camera, {
    matrix = { Camera.__methods.getMatrix, Camera.__methods.setMatrix },
    position = { Camera.__methods.getPosition, Camera.__methods.setPosition },
    target = { "getCameraTarget", "setCameraTarget" },
    interior = { "getCameraInterior", "setCameraInterior" },
    goggleEffect = { "getCameraGoggleEffect", "setCameraGoggleEffect" },
    shakeLevel = { "getCameraShakeLevel", "setCameraShakeLevel" },
})

local Engine = defineClass("Engine", nil, nil)
addMethods(Engine, {
    loadTXD = "engineLoadTXD", loadDFF = "engineLoadDFF", loadCOL = "engineLoadCOL",
    importTXD = "engineImportTXD", replaceModel = "engineReplaceModel", restoreModel = "engineRestoreModel",
    replaceCOL = "engineReplaceCOL", restoreCOL = "engineRestoreCOL",
    requestModel = "engineRequestModel", freeModel = "engineFreeModel",
    getModelLODDistance = "engineGetModelLODDistance", setModelLODDistance = "engineSetModelLODDistance",
    setAsynchronousLoading = "engineSetAsynchronousLoading",
    setRadarMapTile = "engineSetRadarMapTile", resetRadarMapTile = "engineResetRadarMapTile",
    getRadarMapStats = "engineGetRadarMapStats",
    getModelNameFromID = "engineGetModelNameFromID", getModelIDFromName = "engineGetModelIDFromName",
})

-- EngineStreaming (CLuaEngineDefs.cpp:382-399)
local EngineStreaming = defineClass("EngineStreaming", nil, nil)
addMethods(EngineStreaming, {
    freeUpMemory = "engineStreamingFreeUpMemory",
    getUsedMemory = "engineStreamingGetUsedMemory",
    setMemorySize = "engineStreamingSetMemorySize",
    getMemorySize = "engineStreamingGetMemorySize",
    setModelCacheLimits = "engineStreamingSetModelCacheLimits",
    restoreMemorySize = "engineStreamingRestoreMemorySize",
    getBufferSize = "engineStreamingGetBufferSize",
    setBufferSize = "engineStreamingSetBufferSize",
    restoreBufferSize = "engineStreamingRestoreBufferSize",
})
addProps(EngineStreaming, {
    memorySize = { "engineStreamingGetMemorySize", "engineStreamingSetMemorySize" },
    bufferSize = { "engineStreamingGetBufferSize", "engineStreamingSetBufferSize" },
    usedMemory = { "engineStreamingGetUsedMemory", nil },
})

local World = defineClass("World", nil, nil)
addMethods(World, {
    getTime = "getTime", setTime = "setTime",
    getWeather = "getWeather", setWeather = "setWeather", setWeatherBlended = "setWeatherBlended",
    getGravity = "getGravity", setGravity = "setGravity",
    getGameSpeed = "getGameSpeed", setGameSpeed = "setGameSpeed",
    getGroundPosition = "getGroundPosition", getRoofPosition = "getRoofPosition",
    processLineOfSight = "processLineOfSight", isLineOfSightClear = "isLineOfSightClear",
    getWaterLevel = "getWaterLevel", setWaterLevel = "setWaterLevel",
    getSkyGradient = "getSkyGradient", setSkyGradient = "setSkyGradient",
    getHeatHaze = "getHeatHaze", setHeatHaze = "setHeatHaze",
    getFogDistance = "getFogDistance", setFogDistance = "setFogDistance",
    getFarClipDistance = "getFarClipDistance", setFarClipDistance = "setFarClipDistance",
})

local DiscordRPC = defineClass("DiscordRPC", nil, nil)
addMethods(DiscordRPC, {
    setApplicationID = "setDiscordApplicationID",
    resetData = "resetDiscordRichPresenceData",
    setState = "setDiscordRichPresenceState",
    setDetails = "setDiscordRichPresenceDetails",
    setAsset = "setDiscordRichPresenceAsset",
    setButton = "setDiscordRichPresenceButton",
    isConnected = "isDiscordRichPresenceConnected",
})

local pathCls = defineClass("path", nil, nil)
addMethods(pathCls, { listDir = "pathListDir", isFile = "pathIsFile", isDirectory = "pathIsDirectory" })

-- =========================================================================
-- Resource (CLuaResourceDefs.cpp)
-- =========================================================================
local Resource = defineClass("Resource", nil, nil)
addMethods(Resource, {
    getFromName = "getResourceFromName",
    getThis = "getThisResource",
    getAll = "getResources",
    getName = "getResourceName",
    getRootElement = "getResourceRootElement",
    getDynamicElementRoot = "getResourceDynamicElementRoot",
    getGuiRoot = "getResourceGUIElement",
    getState = "getResourceState",
    getExportedFunctions = "getResourceExportedFunctions",
})
addProps(Resource, {
    name = { "getResourceName", nil },
    rootElement = { "getResourceRootElement", nil },
    dynamicElementRoot = { "getResourceDynamicElementRoot", nil },
    guiRoot = { "getResourceGUIElement", nil },
    state = { "getResourceState", nil },
    exportedFunctions = { "getResourceExportedFunctions", nil },
})
resourceMt.__index = function(self, k)
    local g = Resource.__getters[k]
    if g then return g(self) end
    return Resource.__methods[k]
end

-- =========================================================================
-- Handle-Wrapped Classes: Timer, File, XML
-- =========================================================================
local function makeHandleClass(name, createFnName, methods, props)
    local cls = defineClass(name, nil, nil)
    local instMT = {}
    local function wrap(h)
        if not h then return false end
        return setmetatable({ __oopType = name, _handle = h }, instMT)
    end
    cls._wrap = wrap
    for k, fnName in pairs(methods) do
        if type(fnName) == "string" then
            cls.__methods[k] = function(first, ...)
                if type(first) == "table" and rawget(first, "__oopType") == name then
                    return G[fnName](first._handle, unpackOopArgs(...))
                end
                return G[fnName](unpackOopArgs(first, ...))
            end
        else
            cls.__methods[k] = fnName
        end
    end
    for k, spec in pairs(props or {}) do
        if spec[1] then
            local g = spec[1]
            cls.__getters[k] = function(self)
                if not self then return nil end
                return type(g) == "string" and G[g](self._handle) or g(self)
            end
        end
        if spec[2] then
            local s = spec[2]
            cls.__setters[k] = function(self, v)
                if not self then return end
                if type(s) == "string" then G[s](self._handle, unpackOopArgs(v)) else s(self, v) end
            end
        end
    end
    if createFnName then
        cls.__methods.create = function(...)
            return wrap(G[createFnName](unpackOopArgs(...)))
        end
    end
    instMT.__index = function(self, k)
        local g = cls.__getters[k]
        if g then return g(self) end
        return cls.__methods[k]
    end
    instMT.__newindex = function(self, k, v)
        local s = cls.__setters[k]
        if s then s(self, v); return end
        rawset(self, k, v)
    end
    instMT.__eq = function(a, b)
        return type(a) == "table" and type(b) == "table" and a._handle == b._handle
    end
    instMT.__tostring = function(self) return ("%s(%s)"):format(name, tostring(self._handle)) end
    return cls
end

local Timer = makeHandleClass("Timer", "setTimer", {
    isValid = "isTimer",
    destroy = "killTimer",
    reset = "resetTimer",
    getDetails = "getTimerDetails",
    isPaused = "isTimerPaused",
    setPaused = "setTimerPaused",
}, {
    valid = { "isTimer", nil },
    paused = { "isTimerPaused", "setTimerPaused" },
})

local File = makeHandleClass("File", "fileOpen", {
    open = function(...) return G.File._wrap(G.fileOpen(...)) end,
    new = function(...) return G.File._wrap(G.fileCreate(...)) end,
    exists = "fileExists",
    copy = "fileCopy",
    rename = "fileRename",
    delete = "fileDelete",
    close = "fileClose",
    flush = "fileFlush",
    read = "fileRead",
    write = "fileWrite",
    getPos = "fileGetPos",
    setPos = "fileSetPos",
    getSize = "fileGetSize",
    getPath = "fileGetPath",
    getContents = "fileGetContents",
    isEOF = "fileIsEOF",
}, {
    pos = { "fileGetPos", "fileSetPos" },
    size = { "fileGetSize", nil },
    eof = { "fileIsEOF", nil },
    path = { "fileGetPath", nil },
})

local XML = makeHandleClass("XML", "xmlCreateFile", {
    load = function(...) return G.XML._wrap(G.xmlLoadFile(...)) end,
    loadstring = function(...) return G.XML._wrap(G.xmlLoadString(...)) end,
    copy = function(...) return G.XML._wrap(G.xmlCopyFile(...)) end,
    unload = "xmlUnloadFile",
    destroy = "xmlDestroyNode",
    saveFile = "xmlSaveFile",
    createChild = function(self, ...) return G.XML._wrap(G.xmlCreateChild(self._handle, ...)) end,
    findChild = function(self, ...) return G.XML._wrap(G.xmlFindChild(self._handle, ...)) end,
    getParent = function(self) return G.XML._wrap(G.xmlNodeGetParent(self._handle)) end,
    getChildren = function(self, idx)
        if idx ~= nil then return G.XML._wrap(G.xmlNodeGetChildren(self._handle, idx)) end
        local list = G.xmlNodeGetChildren(self._handle)
        if type(list) ~= "table" then return list end
        local out = {}
        for i = 1, #list do out[i] = G.XML._wrap(list[i]) end
        return out
    end,
    getName = "xmlNodeGetName",
    setName = "xmlNodeSetName",
    getValue = "xmlNodeGetValue",
    setValue = "xmlNodeSetValue",
    getAttribute = "xmlNodeGetAttribute",
    setAttribute = "xmlNodeSetAttribute",
    getAttributes = "xmlNodeGetAttributes",
}, {
    name = { "xmlNodeGetName", "xmlNodeSetName" },
    value = { "xmlNodeGetValue", "xmlNodeSetValue" },
    attributes = { "xmlNodeGetAttributes", nil },
    children = { function(self) return G.XML.__methods.getChildren(self) end, nil },
    parent = { function(self) return G.XML.__methods.getParent(self) end, nil },
})
)LUA";

}  // namespace

void AttachElementOopMetatable(lua_State* L) {
    if (!L || !lua_istable(L, -1)) return;
    luaL_newmetatable(L, kElementMt);
    lua_setmetatable(L, -2);
}

void AttachResourceOopMetatable(lua_State* L) {
    if (!L || !lua_istable(L, -1)) return;
    luaL_newmetatable(L, kResourceMt);
    lua_setmetatable(L, -2);
}

void InstallOopLua(lua_State* L) {
    if (!L) return;
    const int top = lua_gettop(L);
    if (luaL_loadbuffer(L, kOopBootstrapLua, sizeof(kOopBootstrapLua) - 1, "@oop_bootstrap") != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        LUA_LOGE("LuaOopApi: compile failed: %s", err ? err : "unknown");
        lua_settop(L, top);
        return;
    }
    luaL_newmetatable(L, kElementMt);
    luaL_newmetatable(L, kResourceMt);
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        LUA_LOGE("LuaOopApi: init failed: %s", err ? err : "unknown");
    }
    lua_settop(L, top);
}

}  // namespace WorldRPG::Script
