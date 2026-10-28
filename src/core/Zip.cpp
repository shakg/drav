#include "Zip.h"

#include <zlib.h>
#include <zstd.h>

#include <cstring>

namespace drav::zip {

namespace {
quint32 u32(const QByteArray& b, qsizetype at)
{
    if (at < 0 || at + 4 > b.size())
        return 0;
    const auto* p = reinterpret_cast<const unsigned char*>(b.constData() + at);
    return quint32(p[0]) | quint32(p[1]) << 8 | quint32(p[2]) << 16 | quint32(p[3]) << 24;
}
quint16 u16(const QByteArray& b, qsizetype at)
{
    if (at < 0 || at + 2 > b.size())
        return 0;
    const auto* p = reinterpret_cast<const unsigned char*>(b.constData() + at);
    return quint16(p[0] | p[1] << 8);
}
} // namespace

QByteArray inflateRaw(const QByteArray& data, qsizetype sizeHint)
{
    z_stream s;
    std::memset(&s, 0, sizeof s);
    if (inflateInit2(&s, -15) != Z_OK)
        return {};
    QByteArray out;
    out.resize(std::max<qsizetype>(sizeHint, data.size() * 4 + 1024));
    s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
    s.avail_in = uInt(data.size());
    int r;
    do {
        if (qsizetype(s.total_out) == out.size())
            out.resize(out.size() * 2);
        s.next_out = reinterpret_cast<Bytef*>(out.data() + s.total_out);
        s.avail_out = uInt(out.size() - qsizetype(s.total_out));
        r = inflate(&s, Z_NO_FLUSH);
    } while (r == Z_OK || (r == Z_BUF_ERROR && s.avail_out == 0));
    // A stream cut short still yields what was decoded.
    const bool ok = r == Z_STREAM_END || (r == Z_BUF_ERROR && s.avail_in == 0 && s.total_out > 0);
    out.resize(qsizetype(s.total_out));
    inflateEnd(&s);
    return ok ? out : QByteArray();
}

QByteArray zstd(const QByteArray& data)
{
    ZSTD_DStream* ds = ZSTD_createDStream();
    if (!ds)
        return {};
    ZSTD_initDStream(ds);
    QByteArray out;
    ZSTD_inBuffer in{data.constData(), size_t(data.size()), 0};
    QByteArray chunk(qsizetype(ZSTD_DStreamOutSize()), Qt::Uninitialized);
    bool ok = true;
    while (in.pos < in.size) {
        ZSTD_outBuffer o{chunk.data(), size_t(chunk.size()), 0};
        const size_t r = ZSTD_decompressStream(ds, &o, &in);
        if (ZSTD_isError(r)) {
            ok = false;
            break;
        }
        out.append(chunk.constData(), qsizetype(o.pos));
        if (r == 0 && in.pos == in.size)
            break;
    }
    ZSTD_freeDStream(ds);
    return ok ? out : QByteArray();
}

QHash<QString, QByteArray> read(const QByteArray& a)
{
    QHash<QString, QByteArray> out;
    // End of central directory record: within the last 64 KiB + 22 bytes.
    qsizetype eocd = -1;
    for (qsizetype i = a.size() - 22; i >= 0 && i >= a.size() - 65557; --i)
        if (u32(a, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0)
        return out;
    const int count = u16(a, eocd + 10);
    qsizetype at = u32(a, eocd + 16);
    for (int i = 0; i < count; ++i) {
        if (u32(a, at) != 0x02014b50)
            break;
        const quint16 method = u16(a, at + 10);
        const quint32 csize = u32(a, at + 20), usize = u32(a, at + 24);
        const quint16 nlen = u16(a, at + 28), elen = u16(a, at + 30), clen = u16(a, at + 32);
        const quint32 local = u32(a, at + 42);
        const QString name = QString::fromUtf8(a.mid(at + 46, nlen));
        at += 46 + nlen + elen + clen;
        if (u32(a, local) != 0x04034b50)
            continue;
        const qsizetype data = local + 30 + u16(a, local + 26) + u16(a, local + 28);
        if (data < 0 || data + qsizetype(csize) > a.size())
            continue;
        const QByteArray raw = a.mid(data, csize);
        if (method == 0)
            out.insert(name, raw);
        else if (method == 8)
            out.insert(name, inflateRaw(raw, usize));
    }
    return out;
}

} // namespace drav::zip
