#pragma once

#include <vector>
#include <mutex>
#include <algorithm>

#include "../maths.hpp"

namespace c2ba
{

class TiledFramebuffer
{
public:
    struct TileBounds
    {
        size_t beginX;
        size_t beginY;
        size_t countX;
        size_t countY;
    };

    TiledFramebuffer() = default;

    TiledFramebuffer(size_t tileSize, size_t imageWidth, size_t imageHeight) :
        m_nTileSize{ tileSize }, m_nTilePixelCount{ m_nTileSize * m_nTileSize },
        m_nImageWidth{ imageWidth }, m_nImageHeight{ imageHeight }, m_nPixelCount{ m_nImageWidth * m_nImageHeight },
        m_nTileCountX{ (m_nImageWidth / m_nTileSize) + ((m_nImageWidth % m_nTileSize) ? 1 : 0) }, m_nTileCountY{ (m_nImageHeight / m_nTileSize) + ((m_nImageHeight % m_nTileSize) ? 1 : 0) },
        m_nTileCount{ m_nTileCountX * m_nTileCountY },
        m_Data(m_nTileCount * m_nTilePixelCount, float4(0.f)),
        m_TileLocks{ m_nTileCount }
    {
    }

    std::unique_lock<std::mutex> lockTile(size_t tileIdx) const
    {
        return std::unique_lock<std::mutex>{ m_TileLocks[tileIdx] };
    }

    float4* tileDataPtr(size_t tileIdx)
    {
        return m_Data.data() + tileIdx * m_nTilePixelCount;
    }

    const float4* tileDataPtr(size_t tileIdx) const
    {
        return m_Data.data() + tileIdx * m_nTilePixelCount;
    }

    TileBounds tileBounds(size_t tileX, size_t tileY) const
    {
        const size_t beginX = tileX * m_nTileSize;
        const size_t endX = std::min((tileX + 1) * m_nTileSize, m_nImageWidth);

        const size_t beginY = tileY * m_nTileSize;
        const size_t endY = std::min((tileY + 1) * m_nTileSize, m_nImageHeight);

        const size_t countX = endX - beginX;
        const size_t countY = endY - beginY;

        assert(countX <= m_nTileSize);
        assert(countY <= m_nTileSize);

        return{ beginX, beginY, countX, countY };
    }

    TileBounds tileBounds(size_t tileIdx) const
    {
        const auto tileX = tileIdx % m_nTileCountX;
        const auto tileY = tileIdx / m_nTileCountX;

        return tileBounds(tileX, tileY);
    }

    void copy(float4 * outImage) const
    {
        for (size_t tileIdx = 0u; tileIdx < m_nTileCount; ++tileIdx)
        {
            const auto bounds = tileBounds(tileIdx);
            const auto tileData = tileDataPtr(tileIdx);

            for (size_t tileY = 0; tileY < bounds.countY; ++tileY) {
                std::copy(tileData + tileY * m_nTileSize, tileData + tileY * m_nTileSize + bounds.countX, outImage + (bounds.beginY + tileY) * m_nImageWidth + bounds.beginX);
            }
        }
    }

    void clear()
    {
        for (size_t tileIdx = 0u; tileIdx < m_nTileCount; ++tileIdx)
        {
            const auto l = lockTile(tileIdx);
            const auto bounds = tileBounds(tileIdx);
            const auto tileData = tileDataPtr(tileIdx);
            std::fill(tileData, tileData + m_nTilePixelCount, float4(0.f));
        }
    }

    size_t tileSize() const
    {
        return m_nTileSize;
    }

    size_t tilePixelCount() const
    {
        return m_nTilePixelCount;
    }

    size_t imageWidth() const
    {
        return m_nImageWidth;
    }

    size_t imageHeight() const
    {
        return m_nImageHeight;
    }

    size_t pixelCount() const
    {
        return m_nPixelCount;
    }

    size_t tileCountX() const
    {
        return m_nTileCountX;
    }

    size_t tileCountY() const
    {
        return m_nTileCountY;
    }

    size_t tileCount() const
    {
        return m_nTileCount;
    }

private:
    size_t m_nTileSize = 0;
    size_t m_nTilePixelCount = 0;
    size_t m_nImageWidth = 0;
    size_t m_nImageHeight = 0;
    size_t m_nPixelCount = 0;

    size_t m_nTileCountX = 0;
    size_t m_nTileCountY = 0;
    size_t m_nTileCount = 0;

    std::vector<float4> m_Data;
    mutable std::vector<std::mutex> m_TileLocks;
};

}