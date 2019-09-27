#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace encoding
{


class timezone
{
public:
    const enum
    {
        type_zone,
        type_coords,
        type_zero
    } type;
    const std::string zone;
    const float latitude;
    const float longitude;

    timezone()
    : type(type_zero)
    , zone()
    , latitude()
    , longitude()
    {}

    timezone(const char* zone_in)
    : type(type_zone)
    , zone(zone_in)
    , latitude(0)
    , longitude(0)
    {}

    timezone(float latitude_in, float longitude_in)
    : type(type_coords)
    , zone("")
    , latitude(latitude_in)
    , longitude(longitude_in)
    {}

    bool operator==(const timezone& them) const
    {
        #define EQ(MEMBER) this->MEMBER == them.MEMBER
        return EQ(type) && EQ(zone) && EQ(latitude) && EQ(longitude);
    }

    std::string to_string() const
    {
        switch(type)
        {
            case timezone::type_zone:
                return zone;
            case timezone::type_coords:
            {
                std::stringstream stream;
                stream << std::fixed << std::setprecision(2) << latitude << "/" << longitude;
                return stream.str();
            }
            case timezone::type_zero:
                break;
        }
        return "";
    }

    friend std::ostream& operator << (std::ostream& os, const timezone& v)
    {
        os << v.to_string();
        return os;
    }
};

class date
{
public:
    const int year;
    const int month;
    const int day;

    date(): date(0, 0, 0) {}

    date(int year_in, int month_in, int day_in)
    : year(year_in)
    , month(month_in)
    , day(day_in)
    {}

    bool operator==(const date& them) const
    {
        #define EQ(MEMBER) this->MEMBER == them.MEMBER
        return EQ(year) && EQ(month) && EQ(day);
    }

    std::string to_string() const
    {
        std::stringstream stream;
        stream << year << "." << std::setfill('0') << std::setw(2) << month << "." << day;
        return stream.str();
    }

    friend std::ostream& operator << (std::ostream& os, const date& v)
    {
        os << v.to_string();
        return os;
    }
};

class time
{
public:
    const int hour;
    const int minute;
    const int second;
    const int nanosecond;
    const timezone tz;

    time(): time(0, 0, 0, 0, timezone()) {}

    time(int hour_in, int minute_in, int second_in, int nanosecond_in, timezone tz_in)
    : hour(hour_in)
    , minute(minute_in)
    , second(second_in)
    , nanosecond(nanosecond_in)
    , tz(tz_in)
    {}

    bool operator==(const time& them) const
    {
        #define EQ(MEMBER) this->MEMBER == them.MEMBER
        return EQ(hour) && EQ(minute) && EQ(second) && EQ(nanosecond) && EQ(tz);
    }

    std::string to_string() const
    {
        std::stringstream stream;
        stream << std::setfill('0') << std::setw(2) << hour << ":" << minute << ":" << second;
        if(nanosecond != 0)
        {
            int subsecond = nanosecond;
            int width = 9;
            while(subsecond % 100 == 0)
            {
                subsecond /= 1000;
                width -= 3;
            }
            stream << "." << std::setw(width) << subsecond;
        }
        if(tz.type != timezone::type_zero)
        {
            stream << "/" << tz;
        }
        return stream.str();
    }

    friend std::ostream& operator << (std::ostream& os, const time& v)
    {
        os << v.to_string();
        return os;
    }
};

class timestamp
{
public:
    const int year;
    const int month;
    const int day;
    const int hour;
    const int minute;
    const int second;
    const int nanosecond;
    const timezone tz;

    timestamp(): timestamp(0, 0, 0, 0, 0, 0, 0, timezone()) {}

    timestamp(
        int year_in,
        int month_in,
        int day_in,
        int hour_in,
        int minute_in,
        int second_in,
        int nanosecond_in,
        timezone tz_in)
    : year(year_in)
    , month(month_in)
    , day(day_in)
    , hour(hour_in)
    , minute(minute_in)
    , second(second_in)
    , nanosecond(nanosecond_in)
    , tz(tz_in)
    {}

    bool operator==(const timestamp& them) const
    {
        #define EQ(MEMBER) this->MEMBER == them.MEMBER
        return EQ(year) && EQ(month) && EQ(day) && EQ(hour) && EQ(minute) && EQ(second) && EQ(nanosecond) && EQ(tz);
    }

    std::string to_string() const
    {
        std::stringstream stream;
        stream << year << "." << std::setfill('0') << std::setw(2) << month << "." << day << "-";
        stream << hour << ":" << minute << ":" << second;
        if(nanosecond != 0)
        {
            int subsecond = nanosecond;
            int width = 9;
            while(subsecond % 100 == 0)
            {
                subsecond /= 1000;
                width -= 3;
            }
            stream << "." << std::setw(width) << subsecond;
        }
        if(tz.type != timezone::type_zero)
        {
            stream << "/" << tz;
        }
        return stream.str();
    }

    friend std::ostream& operator << (std::ostream& os, const timestamp& v)
    {
        os << v.to_string();
        return os;
    }
};

static std::ostream& operator<<(std::ostream& os, const std::vector<unsigned char> &v)
{
    auto flags = os.flags();
    for(int i = 0; i < (int)v.size(); i++)
    {
        int value = v[i];
        os << "0x" << std::setw(2) << std::setfill('0') << std::hex << value;
        if(i < (int)v.size()-1)
        {
            os << ", ";
        }
    }
    os.flags(flags);
    return os;
}

class value
{
public:
    typedef enum
    {
        type_int_pos,
        type_int_neg,
        type_float,
        type_bool,
        type_date,
        type_time,
        type_ts,
        type_str,
        type_bin,
        type_uri,
        type_com,
        type_strh,
        type_binh,
        type_urih,
        type_comh,
        type_data,
        type_list,
        type_map_u,
        type_map_o,
        type_map_m,
        type_end,
        type_nil,
        type_pad,
    } value_type;

    const value_type type;

    const uint64_t i;
    const double f;
    const date d;
    const time t;
    const timestamp ts;
    const bool b;
    const std::string str;
    const std::vector<unsigned char> bin;

    value(value_type type_in, uint64_t value): type(type_in), i(value), f(), d(), t(), ts(), b(), str(), bin() {}
    value(value_type type_in, double value): type(type_in), i(), f(value), d(), t(), ts(), b(), str(), bin() {}
    value(value_type type_in, date value): type(type_in), i(), f(), d(value), t(), ts(), b(), str(), bin() {}
    value(value_type type_in, time value): type(type_in), i(), f(), d(), t(value), ts(), b(), str(), bin() {}
    value(value_type type_in, timestamp value): type(type_in), i(), f(), d(), t(), ts(value), b(), str(), bin() {}
    value(value_type type_in, bool value): type(type_in), i(), f(), d(), t(), ts(), b(value), str(), bin() {}
    value(value_type type_in, std::string value): type(type_in), i(), f(), d(), t(), ts(), b(), str(value), bin() {}
    value(value_type type_in, std::vector<unsigned char> value): type(type_in), i(), f(), d(), t(), ts(), b(), str(), bin(value) {}

    bool operator==(const value& them) const
    {
        #define EQ(MEMBER) this->MEMBER == them.MEMBER
        return EQ(type) && EQ(i) && EQ(f) && EQ(d) && EQ(t) && EQ(ts) && EQ(b) && EQ(str) && EQ(bin);
    }

    std::string to_string() const
    {
        std::stringstream stream;
        switch(type)
        {
            case type_int_pos:
                stream << "i(" << i << ")";
                break;
            case type_int_neg:
                stream << "i(-" << i << ")";
                break;
            case type_float:
                stream << "f(" << f << ")";
                break;
            case type_bool:
                stream << "b(" << (b ? "true" : "false") << ")";
                break;
            case type_date:
                stream << "d(" << d << ")";
                break;
            case type_time:
                stream << "t(" << t << ")";
                break;
            case type_ts:
                stream << "ts(" << ts << ")";
                break;
            case type_str:
                stream << "str(\"" << str << "\")";
                break;
            case type_bin:
                stream << "bin({" << bin << "})";
                break;
            case type_uri:
                stream << "uri(\"" << str << "\")";
                break;
            case type_com:
                stream << "com(\"" << str << "\")";
                break;
            case type_strh:
                stream << "strh(" << i << ")";
                break;
            case type_binh:
                stream << "binh(" << i << ")";
                break;
            case type_urih:
                stream << "urih(" << i << ")";
                break;
            case type_comh:
                stream << "comh(" << i << ")";
                break;
            case type_data:
                stream << "data({" << bin << "})";
                break;
            case type_list:
                stream << "list()";
                break;
            case type_map_u:
                stream << "umap()";
                break;
            case type_map_o:
                stream << "omap()";
                break;
            case type_map_m:
                stream << "mmap()";
                break;
            case type_end:
                stream << "end()";
                break;
            case type_nil:
                stream << "nil()";
                break;
            case type_pad:
                stream << "pad(" << i << ")";
                break;
        }
        return stream.str();
    }

    friend std::ostream& operator << (std::ostream& os, const value& v)
    {
        os << v.to_string();
        return os;
    }

    static value iv(int64_t v)
    {
        if(v >= 0)
        {
            return value(type_int_pos, (uint64_t)v);
        }
        return value(type_int_neg, (uint64_t)-v);
    }
    static value iv(int sign, uint64_t v)
    {
        if(sign >= 0)
        {
            return value(type_int_pos, v);
        }
        return value(type_int_neg, v);
    }
    static value uv(uint64_t v) {return value(type_int_pos, v);}
    static value fv(double v) {return value(type_float, v);}
    static value bv(bool v) {return value(type_bool, v);}
    static value dv(date v) {return value(type_date, v);}
    static value dv(int year, int month, int day) {return value(type_date, date(year, month, day));}
    static value tv(time v) {return value(type_time, v);}
    static value tv(int hour, int minute, int second, int nanosecond, const char* tz)
    {return value(type_time, time(hour, minute, second, nanosecond, timezone(tz)));}
    static value tv(int hour, int minute, int second, int nanosecond, float latitude, float longitude)
    {return value(type_time, time(hour, minute, second, nanosecond, timezone(latitude, longitude)));}
    static value tsv(timestamp v) {return value(type_ts, v);}
    static value tsv(int year, int month, int day, int hour, int minute, int second, int nanosecond, const char* tz)
    {return value(type_ts, timestamp(year, month, day, hour, minute, second, nanosecond, timezone(tz)));}
    static value tsv(int year, int month, int day, int hour, int minute, int second, int nanosecond, float latitude, float longitude)
    {return value(type_ts, timestamp(year, month, day, hour, minute, second, nanosecond, timezone(latitude, longitude)));}
    static value strv(std::string v) {return value(type_str, v);}
    static value binv(std::vector<unsigned char> v) {return value(type_bin, v);}
    static value uriv(std::string v) {return value(type_uri, v);}
    static value comv(std::string v) {return value(type_com, v);}
    static value strhv(uint64_t v) {return value(type_strh, v);}
    static value binhv(uint64_t v) {return value(type_binh, v);}
    static value urihv(uint64_t v) {return value(type_urih, v);}
    static value comhv(uint64_t v) {return value(type_comh, v);}
    static value datav(std::vector<unsigned char> v) {return value(type_data, v);}
    static value listv() {return value(type_list, (uint64_t)0);}
    static value umapv() {return value(type_map_u, (uint64_t)0);}
    static value omapv() {return value(type_map_o, (uint64_t)0);}
    static value mmapv() {return value(type_map_m, (uint64_t)0);}
    static value endv() {return value(type_end, (uint64_t)0);}
    static value nilv() {return value(type_nil, (uint64_t)0);}
    static value padv(unsigned bytes) {return value(type_pad, (uint64_t)bytes);}
};


class enc
{
public:
    std::vector<value> values;

    std::string to_string() const
    {
        std::stringstream stream;
        for(int i = 0; i < (int)values.size(); i++)
        {
            stream << values[i];
            if(i < (int)values.size() - 1)
            {
                stream << ".";
            }
        }

        return stream.str();
    }

    bool operator==(const enc& them) const
    {
        return this->values == them.values;
    }

    bool operator!=(const enc& them) const
    {
        return *this != them;
    }

    friend std::ostream& operator << (std::ostream& os, const enc& v)
    {
        os << v.to_string();
        return os;
    }

    enc add(value v)
    {
        values.push_back(v);
        return *this;
    }

    #define DEFINE_INITIATOR_0(NAME) enc NAME() {return add(value::NAME##v());}
    #define DEFINE_INITIATOR_1(NAME, TYPE) enc NAME(TYPE v) {return add(value::NAME##v(v));}
    DEFINE_INITIATOR_1(i, int64_t)
    DEFINE_INITIATOR_1(u, uint64_t)
    DEFINE_INITIATOR_1(f, double)
    DEFINE_INITIATOR_1(b, bool)
    DEFINE_INITIATOR_1(d, date)
    DEFINE_INITIATOR_1(t, time)
    DEFINE_INITIATOR_1(ts, timestamp)
    DEFINE_INITIATOR_1(str, std::string)
    DEFINE_INITIATOR_1(uri, std::string)
    DEFINE_INITIATOR_1(com, std::string)
    DEFINE_INITIATOR_1(bin, std::vector<uint8_t>)
    DEFINE_INITIATOR_1(strh, uint64_t)
    DEFINE_INITIATOR_1(urih, uint64_t)
    DEFINE_INITIATOR_1(comh, uint64_t)
    DEFINE_INITIATOR_1(binh, uint64_t)
    DEFINE_INITIATOR_1(data, std::vector<uint8_t>)
    DEFINE_INITIATOR_0(list)
    DEFINE_INITIATOR_0(umap)
    DEFINE_INITIATOR_0(omap)
    DEFINE_INITIATOR_0(mmap)
    DEFINE_INITIATOR_0(end)
    DEFINE_INITIATOR_0(nil)
    DEFINE_INITIATOR_1(pad, unsigned)
    #undef DEFINE_INITIATOR_0
    #undef DEFINE_INITIATOR_1

    enc i(int sign, uint64_t v) {return add(value::iv(sign, v));}
    enc d(int year, int month, int day) {return add(value::dv(year, month, day));}
    enc t(int hour, int minute, int second, int nanosecond, const char* tz)
    {return add(value::tv(hour, minute, second, nanosecond, tz));}
    enc t(int hour, int minute, int second, int nanosecond, float latitude, float longitude)
    {return add(value::tv(hour, minute, second, nanosecond, latitude, longitude));}
    enc ts(int year, int month, int day, int hour, int minute, int second, int nanosecond, const char* tz)
    {return add(value::tsv(year, month, day, hour, minute, second, nanosecond, tz));}
    enc ts(int year, int month, int day, int hour, int minute, int second, int nanosecond, float latitude, float longitude)
    {return add(value::tsv(year, month, day, hour, minute, second, nanosecond, latitude, longitude));}
};


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

#define DEFINE_INITIATOR_0(NAME) static enc NAME() {return enc().add(value::NAME##v());}
#define DEFINE_INITIATOR_1(NAME, TYPE) static enc NAME(TYPE v) {return enc().add(value::NAME##v(v));}
DEFINE_INITIATOR_1(i, int64_t)
DEFINE_INITIATOR_1(u, uint64_t)
DEFINE_INITIATOR_1(f, double)
DEFINE_INITIATOR_1(b, bool)
DEFINE_INITIATOR_1(d, date)
DEFINE_INITIATOR_1(t, time)
DEFINE_INITIATOR_1(ts, timestamp)
DEFINE_INITIATOR_1(str, std::string)
DEFINE_INITIATOR_1(uri, std::string)
DEFINE_INITIATOR_1(com, std::string)
DEFINE_INITIATOR_1(bin, std::vector<uint8_t>)
DEFINE_INITIATOR_1(strh, uint64_t)
DEFINE_INITIATOR_1(urih, uint64_t)
DEFINE_INITIATOR_1(comh, uint64_t)
DEFINE_INITIATOR_1(binh, uint64_t)
DEFINE_INITIATOR_1(data, std::vector<uint8_t>)
DEFINE_INITIATOR_0(list)
DEFINE_INITIATOR_0(umap)
DEFINE_INITIATOR_0(omap)
DEFINE_INITIATOR_0(mmap)
DEFINE_INITIATOR_0(end)
DEFINE_INITIATOR_0(nil)
DEFINE_INITIATOR_1(pad, unsigned)
#undef DEFINE_INITIATOR_0
#undef DEFINE_INITIATOR_1

static enc i(int sign, uint64_t v) {return enc().add(value::iv(sign, v));}
static enc d(int year, int month, int day) {return enc().add(value::dv(year, month, day));}
static enc t(int hour, int minute, int second, int nanosecond, const char* tz)
{return enc().add(value::tv(hour, minute, second, nanosecond, tz));}
static enc t(int hour, int minute, int second, int nanosecond, float latitude, float longitude)
{return enc().add(value::tv(hour, minute, second, nanosecond, latitude, longitude));}
static enc ts(int year, int month, int day, int hour, int minute, int second, int nanosecond, const char* tz)
{return enc().add(value::tsv(year, month, day, hour, minute, second, nanosecond, tz));}
static enc ts(int year, int month, int day, int hour, int minute, int second, int nanosecond, float latitude, float longitude)
{return enc().add(value::tsv(year, month, day, hour, minute, second, nanosecond, latitude, longitude));}

#pragma GCC diagnostic pop
}