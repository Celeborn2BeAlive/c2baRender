#pragma once

namespace c2ba
{

template<typename DeleteFunc>
struct RAII
{
    DeleteFunc m_DelF;
    RAII(DeleteFunc delF) : m_DelF(std::move(delF))
    {
    }

    ~RAII()
    {
        m_DelF();
    }

    RAII(const RAII &) = delete;
    RAII & operator =(const RAII &) = delete;
    RAII(RAII &&) = default;
    RAII & operator =(RAII &&) = default;
};

template<typename DeleteFunc>
inline RAII<DeleteFunc> finally(DeleteFunc && delF)
{
    return RAII<DeleteFunc>(delF);
}

}