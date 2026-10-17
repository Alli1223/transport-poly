#pragma once
// Binary serialisation used for save games, network packets and command payloads.
//
// Types opt in with a member template:
//     template <class Ar> void serialize(Ar& ar) { ar(fieldA, fieldB); }
// The same function both writes (tp::Writer) and reads (tp::Reader). Integers are stored
// little endian with their full width, container sizes as LEB128 varints.

#include "core/types.hpp"

#include <array>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace tp {

class SerializeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ByteWriter {
public:
    void bytes(const void* data, usize size) {
        const auto* p = static_cast<const u8*>(data);
        buf_.insert(buf_.end(), p, p + size);
    }
    template <std::unsigned_integral T>
    void le(T v) {
        for (usize i = 0; i < sizeof(T); ++i) buf_.push_back(static_cast<u8>(v >> (8 * i)));
    }
    void varint(u64 v) {
        while (v >= 0x80) {
            buf_.push_back(static_cast<u8>(v | 0x80));
            v >>= 7;
        }
        buf_.push_back(static_cast<u8>(v));
    }
    void string(std::string_view s) {
        varint(s.size());
        bytes(s.data(), s.size());
    }

    const std::vector<u8>& data() const { return buf_; }
    std::vector<u8>& data() { return buf_; }
    std::vector<u8> take() { return std::move(buf_); }
    usize size() const { return buf_.size(); }
    void clear() { buf_.clear(); }

private:
    std::vector<u8> buf_;
};

class ByteReader {
public:
    explicit ByteReader(std::span<const u8> data) : data_(data) {}

    void bytes(void* out, usize size) {
        need(size);
        if (size) std::memcpy(out, data_.data() + pos_, size);
        pos_ += size;
    }
    template <std::unsigned_integral T>
    T le() {
        need(sizeof(T));
        T v = 0;
        for (usize i = 0; i < sizeof(T); ++i) v |= static_cast<T>(static_cast<T>(data_[pos_ + i]) << (8 * i));
        pos_ += sizeof(T);
        return v;
    }
    u64 varint() {
        u64 v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            need(1);
            const u8 b = data_[pos_++];
            v |= static_cast<u64>(b & 0x7f) << shift;
            if (!(b & 0x80)) return v;
        }
        throw SerializeError("malformed varint");
    }
    std::string string(usize maxLength = 1u << 24) {
        const u64 len = varint();
        if (len > maxLength) throw SerializeError("string too long");
        need(static_cast<usize>(len));
        std::string s(reinterpret_cast<const char*>(data_.data() + pos_), static_cast<usize>(len));
        pos_ += static_cast<usize>(len);
        return s;
    }

    usize remaining() const { return data_.size() - pos_; }
    usize position() const { return pos_; }
    bool atEnd() const { return pos_ == data_.size(); }

private:
    void need(usize n) const {
        if (remaining() < n) throw SerializeError("unexpected end of data");
    }

    std::span<const u8> data_;
    usize pos_ = 0;
};

namespace detail {
template <class T>
inline constexpr bool kIsVector = false;
template <class T, class A>
inline constexpr bool kIsVector<std::vector<T, A>> = true;
template <class T>
inline constexpr bool kIsArray = false;
template <class T, usize N>
inline constexpr bool kIsArray<std::array<T, N>> = true;
template <class T>
inline constexpr bool kIsOptional = false;
template <class T>
inline constexpr bool kIsOptional<std::optional<T>> = true;
template <class T>
inline constexpr bool kIsPair = false;
template <class A, class B>
inline constexpr bool kIsPair<std::pair<A, B>> = true;
template <class T>
inline constexpr bool kIsMap = false;
template <class K, class V, class C, class A>
inline constexpr bool kIsMap<std::map<K, V, C, A>> = true;
template <class T>
inline constexpr bool kIsSet = false;
template <class K, class C, class A>
inline constexpr bool kIsSet<std::set<K, C, A>> = true;
template <class T>
inline constexpr bool kIsVariant = false;
template <class... Ts>
inline constexpr bool kIsVariant<std::variant<Ts...>> = true;
template <class>
inline constexpr bool kAlwaysFalse = false;

template <class T, class Ar>
concept HasSerialize = requires(T& t, Ar& ar) { t.serialize(ar); };
} // namespace detail

// Writing archive.
class Writer {
public:
    static constexpr bool kReading = false;

    explicit Writer(ByteWriter& out, u32 formatVersion = 0) : version(formatVersion), out_(out) {}

    template <class... Ts>
    void operator()(const Ts&... values) {
        (write(values), ...);
    }

    template <class T>
    void write(const T& v) {
        if constexpr (std::is_same_v<T, std::monostate>) {
            // nothing to write
        } else if constexpr (std::is_same_v<T, bool>) {
            out_.le<u8>(v ? 1 : 0);
        } else if constexpr (std::is_enum_v<T>) {
            write(static_cast<std::underlying_type_t<T>>(v));
        } else if constexpr (std::is_integral_v<T>) {
            out_.le(static_cast<std::make_unsigned_t<T>>(v));
        } else if constexpr (std::is_same_v<T, std::string>) {
            out_.string(v);
        } else if constexpr (detail::kIsVector<T>) {
            out_.varint(v.size());
            for (const auto& e : v) write(static_cast<const typename T::value_type&>(e));
        } else if constexpr (detail::kIsArray<T>) {
            for (const auto& e : v) write(e);
        } else if constexpr (detail::kIsOptional<T>) {
            write(v.has_value());
            if (v) write(*v);
        } else if constexpr (detail::kIsPair<T>) {
            write(v.first);
            write(v.second);
        } else if constexpr (detail::kIsMap<T> || detail::kIsSet<T>) {
            out_.varint(v.size());
            for (const auto& e : v) write(e);
        } else if constexpr (detail::kIsVariant<T>) {
            out_.varint(v.index());
            std::visit([this](const auto& alt) { write(alt); }, v);
        } else if constexpr (detail::HasSerialize<T, Writer>) {
            const_cast<T&>(v).serialize(*this);
        } else {
            static_assert(detail::kAlwaysFalse<T>, "type is not serialisable");
        }
    }

    ByteWriter& bytes() { return out_; }

    u32 version; // format version, lets serialize() functions stay backwards compatible

private:
    ByteWriter& out_;
};

// Reading archive. Throws SerializeError on malformed input.
class Reader {
public:
    static constexpr bool kReading = true;

    explicit Reader(ByteReader& in, u32 formatVersion = 0) : version(formatVersion), in_(in) {}

    template <class... Ts>
    void operator()(Ts&... values) {
        (read(values), ...);
    }

    template <class T>
    void read(T& v) {
        if constexpr (std::is_same_v<T, std::monostate>) {
            // nothing to read
        } else if constexpr (std::is_same_v<T, bool>) {
            const u8 b = in_.le<u8>();
            if (b > 1) throw SerializeError("invalid bool");
            v = b != 0;
        } else if constexpr (std::is_enum_v<T>) {
            std::underlying_type_t<T> u{};
            read(u);
            v = static_cast<T>(u);
        } else if constexpr (std::is_integral_v<T>) {
            v = static_cast<T>(in_.le<std::make_unsigned_t<T>>());
        } else if constexpr (std::is_same_v<T, std::string>) {
            v = in_.string();
        } else if constexpr (detail::kIsVector<T>) {
            const usize n = readCount();
            v.clear();
            v.resize(n);
            if constexpr (std::is_same_v<T, std::vector<bool>>) {
                for (usize i = 0; i < n; ++i) {
                    bool b = false;
                    read(b);
                    v[i] = b;
                }
            } else {
                for (auto& e : v) read(e);
            }
        } else if constexpr (detail::kIsArray<T>) {
            for (auto& e : v) read(e);
        } else if constexpr (detail::kIsOptional<T>) {
            bool has = false;
            read(has);
            if (has) {
                v.emplace();
                read(*v);
            } else {
                v.reset();
            }
        } else if constexpr (detail::kIsPair<T>) {
            read(v.first);
            read(v.second);
        } else if constexpr (detail::kIsMap<T>) {
            const usize n = readCount();
            v.clear();
            for (usize i = 0; i < n; ++i) {
                typename T::key_type key{};
                typename T::mapped_type value{};
                read(key);
                read(value);
                v.emplace_hint(v.end(), std::move(key), std::move(value));
            }
        } else if constexpr (detail::kIsSet<T>) {
            const usize n = readCount();
            v.clear();
            for (usize i = 0; i < n; ++i) {
                typename T::key_type key{};
                read(key);
                v.emplace_hint(v.end(), std::move(key));
            }
        } else if constexpr (detail::kIsVariant<T>) {
            const u64 index = in_.varint();
            if (index >= std::variant_size_v<T>) throw SerializeError("invalid variant index");
            readVariant(v, static_cast<usize>(index), std::make_index_sequence<std::variant_size_v<T>>{});
        } else if constexpr (detail::HasSerialize<T, Reader>) {
            v.serialize(*this);
        } else {
            static_assert(detail::kAlwaysFalse<T>, "type is not serialisable");
        }
    }

    ByteReader& bytes() { return in_; }

    u32 version;

private:
    usize readCount() {
        const u64 n = in_.varint();
        // Every element needs at least one byte, except for degenerate empty types.
        if (n > in_.remaining() && n > 4096) throw SerializeError("container size exceeds input");
        return static_cast<usize>(n);
    }

    template <class V, usize... I>
    void readVariant(V& v, usize index, std::index_sequence<I...>) {
        (void)((index == I ? (v.template emplace<I>(), read(std::get<I>(v)), true) : false) || ...);
    }

    ByteReader& in_;
};

template <class T>
std::vector<u8> toBytes(const T& value, u32 version = 0) {
    ByteWriter bw;
    Writer w(bw, version);
    w(value);
    return bw.take();
}

template <class T>
void fromBytes(std::span<const u8> data, T& value, u32 version = 0) {
    ByteReader br(data);
    Reader r(br, version);
    r(value);
    if (!br.atEnd()) throw SerializeError("trailing data");
}

} // namespace tp
