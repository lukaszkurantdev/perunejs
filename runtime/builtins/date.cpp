#include "runtime/builtins.h"

#include "memory/js_date.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace perunejs {
    namespace {
        constexpr double MS_PER_SECOND = 1000;
        constexpr double MS_PER_MINUTE = 60 * MS_PER_SECOND;
        constexpr double MS_PER_HOUR   = 60 * MS_PER_MINUTE;
        constexpr double MS_PER_DAY    = 24 * MS_PER_HOUR;

        constexpr double MAX_TIME = 8.64e15;

        JSValue nan_value() { return JSValue::number(std::nan("")); }

        double modulo(const double x, const double y) {
            const double rest = std::fmod(x, y);
            return rest < 0 ? rest + y : rest;
        }

        double day(const double t)             { return std::floor(t / MS_PER_DAY); }
        double time_within_day(const double t) { return modulo(t, MS_PER_DAY); }

        bool is_leap_year(const double year) {
            return std::fmod(year, 4) == 0
                && (std::fmod(year, 100) != 0 || std::fmod(year, 400) == 0);
        }

        double day_from_year(const double year) {
            return 365 * (year - 1970)
                 + std::floor((year - 1969) / 4)
                 - std::floor((year - 1901) / 100)
                 + std::floor((year - 1601) / 400);
        }

        double time_from_year(const double year) { return MS_PER_DAY * day_from_year(year); }

        double year_from_time(const double t) {
            double year = std::floor(t / (MS_PER_DAY * 365.2425)) + 1970;

            while (time_from_year(year) > t) year -= 1;
            while (time_from_year(year + 1) <= t) year += 1;

            return year;
        }

        const int MONTH_DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

        int days_in_month(const int month, const bool leap) {
            return month == 1 && leap ? 29 : MONTH_DAYS[month];
        }

        double day_within_year(const double t) {
            return day(t) - day_from_year(year_from_time(t));
        }

        int month_from_time(const double t) {
            const bool leap = is_leap_year(year_from_time(t));

            double left = day_within_year(t);
            for (int month = 0; month < 12; ++month) {
                const int length = days_in_month(month, leap);
                if (left < length) return month;

                left -= length;
            }

            return 11;
        }

        double date_from_time(const double t) {
            const bool leap = is_leap_year(year_from_time(t));

            double left = day_within_year(t);
            for (int month = 0; month < 12; ++month) {
                const int length = days_in_month(month, leap);
                if (left < length) return left + 1;

                left -= length;
            }

            return left + 1;
        }

        int week_day(const double t) { return static_cast<int>(modulo(day(t) + 4, 7)); }

        double hour_from_time(const double t) { return modulo(std::floor(t / MS_PER_HOUR), 24); }
        double min_from_time(const double t)  { return modulo(std::floor(t / MS_PER_MINUTE), 60); }
        double sec_from_time(const double t)  { return modulo(std::floor(t / MS_PER_SECOND), 60); }
        double ms_from_time(const double t)   { return modulo(t, MS_PER_SECOND); }

        double local_offset(const double utc) {
            if (!std::isfinite(utc)) return 0;

            const auto seconds = static_cast<time_t>(std::floor(utc / MS_PER_SECOND));

            std::tm local{};
            if (localtime_r(&seconds, &local) == nullptr) return 0;

            return static_cast<double>(local.tm_gmtoff) * MS_PER_SECOND;
        }

        double local_time(const double t) {
            if (std::isnan(t)) return t;

            return t + local_offset(t);
        }

        double utc_from_local(const double t) {
            if (std::isnan(t)) return t;

            const double earlier = t - local_offset(t - MS_PER_DAY);
            const double later   = t - local_offset(t + MS_PER_DAY);

            if (local_time(earlier) == t) return earlier;
            if (local_time(later) == t)   return later;

            return earlier;
        }

        double make_time(const double hour, const double minute,
                         const double second, const double ms) {
            if (!std::isfinite(hour) || !std::isfinite(minute)
                || !std::isfinite(second) || !std::isfinite(ms)) {
                return std::nan("");
            }

            return std::trunc(hour) * MS_PER_HOUR + std::trunc(minute) * MS_PER_MINUTE
                 + std::trunc(second) * MS_PER_SECOND + std::trunc(ms);
        }

        double make_day(const double year, const double month, const double date) {
            if (!std::isfinite(year) || !std::isfinite(month) || !std::isfinite(date)) {
                return std::nan("");
            }

            const double whole_year = std::trunc(year) + std::floor(std::trunc(month) / 12);
            const int whole_month = static_cast<int>(modulo(std::trunc(month), 12));

            if (std::abs(whole_year) > 400000) return std::nan("");

            double days = day_from_year(whole_year);
            const bool leap = is_leap_year(whole_year);

            for (int i = 0; i < whole_month; ++i) days += days_in_month(i, leap);

            return days + std::trunc(date) - 1;
        }

        double make_date(const double days, const double time) {
            if (!std::isfinite(days) || !std::isfinite(time)) return std::nan("");

            return days * MS_PER_DAY + time;
        }

        double time_clip(const double t) {
            if (!std::isfinite(t) || std::abs(t) > MAX_TIME) return std::nan("");

            return std::trunc(t) + 0;
        }

        double now_milliseconds() {
            return std::floor(static_cast<double>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()));
        }

        const char *WEEK_DAYS[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        const char *MONTHS[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

        std::string padded(const double value, const int width) {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%0*d", width, static_cast<int>(value));

            return buffer;
        }

        std::string signed_year(const double year) {
            if (year >= 0) return padded(year, 4);

            return "-" + padded(-year, 6);
        }

        std::string date_part(const double t) {
            return std::string(WEEK_DAYS[week_day(t)]) + " " + MONTHS[month_from_time(t)] + " "
                 + padded(date_from_time(t), 2) + " " + signed_year(year_from_time(t));
        }

        std::string clock_part(const double t) {
            return padded(hour_from_time(t), 2) + ":" + padded(min_from_time(t), 2)
                 + ":" + padded(sec_from_time(t), 2);
        }

        std::string zone_part(const double t) {
            const double offset = local_offset(t);
            const double minutes = std::abs(offset) / MS_PER_MINUTE;

            std::string text = std::string("GMT") + (offset < 0 ? "-" : "+")
                             + padded(std::floor(minutes / 60), 2)
                             + padded(std::fmod(minutes, 60), 2);

            const auto seconds = static_cast<time_t>(std::floor(t / MS_PER_SECOND));
            std::tm local{};

            if (localtime_r(&seconds, &local) != nullptr && local.tm_zone != nullptr) {
                text += std::string(" (") + local.tm_zone + ")";
            }

            return text;
        }

        struct Cursor {
            const std::string &text;
            std::size_t at = 0;

            bool done() const { return at >= text.size(); }
            char peek() const { return at < text.size() ? text[at] : '\0'; }

            bool take(const char expected) {
                if (peek() != expected) return false;

                ++at;
                return true;
            }

            bool digits(const int count, double &out) {
                if (at + count > text.size()) return false;

                double value = 0;
                for (int i = 0; i < count; ++i) {
                    const char c = text[at + i];
                    if (c < '0' || c > '9') return false;

                    value = value * 10 + (c - '0');
                }

                at += count;
                out = value;
                return true;
            }
        };

        bool parse_iso(const std::string &text, double &out) {
            Cursor cursor{text};

            double year = 0;
            double sign = 1;

            if (cursor.peek() == '+' || cursor.peek() == '-') {
                sign = cursor.peek() == '-' ? -1 : 1;
                ++cursor.at;

                if (!cursor.digits(6, year)) return false;

                if (sign < 0 && year == 0) return false;
            } else if (!cursor.digits(4, year)) {
                return false;
            }

            year *= sign;

            double month = 1;
            double date = 1;

            if (cursor.take('-')) {
                if (!cursor.digits(2, month)) return false;

                if (cursor.take('-') && !cursor.digits(2, date)) return false;
            }

            double hour = 0, minute = 0, second = 0, ms = 0;
            bool has_zone = false;
            double zone_offset = 0;

            if (cursor.take('T')) {
                if (!cursor.digits(2, hour)) return false;
                if (!cursor.take(':')) return false;
                if (!cursor.digits(2, minute)) return false;

                if (cursor.take(':')) {
                    if (!cursor.digits(2, second)) return false;

                    if (cursor.take('.')) {
                        if (!cursor.digits(3, ms)) return false;
                    }
                }

                if (cursor.take('Z')) {
                    has_zone = true;
                } else if (cursor.peek() == '+' || cursor.peek() == '-') {
                    const double zone_sign = cursor.peek() == '-' ? -1 : 1;
                    ++cursor.at;

                    double zone_hour = 0, zone_minute = 0;
                    if (!cursor.digits(2, zone_hour)) return false;
                    if (!cursor.take(':')) return false;
                    if (!cursor.digits(2, zone_minute)) return false;

                    has_zone = true;
                    zone_offset = zone_sign * (zone_hour * MS_PER_HOUR + zone_minute * MS_PER_MINUTE);
                }
            }

            if (!cursor.done()) return false;

            if (month < 1 || month > 12 || date < 1 || date > 31) return false;
            if (hour > 24 || minute > 59 || second > 59) return false;

            const double value = make_date(make_day(year, month - 1, date),
                                           make_time(hour, minute, second, ms));

            (void) has_zone;

            out = time_clip(value - zone_offset);
            return true;
        }

        int month_index(const std::string &name) {
            for (int i = 0; i < 12; ++i) {
                if (name == MONTHS[i]) return i;
            }

            return -1;
        }

        bool parse_textual(const std::string &text, double &out) {
            char month_name[16] = {0};
            char zone_sign = 0;
            int date = 0, year = 0, hour = 0, minute = 0, second = 0, zone = 0;

            if (std::sscanf(text.c_str(), "%*3s %15s %d %d %d:%d:%d GMT%c%4d",
                            month_name, &date, &year, &hour, &minute, &second,
                            &zone_sign, &zone) == 8) {
                const int month = month_index(month_name);
                if (month < 0) return false;

                const double offset = (zone_sign == '-' ? -1 : 1)
                                    * ((zone / 100) * MS_PER_HOUR + (zone % 100) * MS_PER_MINUTE);

                out = time_clip(make_date(make_day(year, month, date),
                                          make_time(hour, minute, second, 0)) - offset);
                return true;
            }

            if (std::sscanf(text.c_str(), "%*3s, %d %15s %d %d:%d:%d GMT",
                            &date, month_name, &year, &hour, &minute, &second) == 6) {
                const int month = month_index(month_name);
                if (month < 0) return false;

                out = time_clip(make_date(make_day(year, month, date),
                                          make_time(hour, minute, second, 0)));
                return true;
            }

            if (std::sscanf(text.c_str(), "%*3s %15s %d %d", month_name, &date, &year) == 3) {
                const int month = month_index(month_name);
                if (month < 0) return false;

                out = time_clip(utc_from_local(make_date(make_day(year, month, date), 0)));
                return true;
            }

            return false;
        }

        double parse_date(const std::string &text) {
            double value = 0;

            if (parse_iso(text, value))     return value;
            if (parse_textual(text, value)) return value;

            return std::nan("");
        }

        Completion this_date(Evaluator &evaluator, const JSValue &this_value, JSDate *&out) {
            if (this_value.type() == JSValueType::Object) {
                if (auto *date = dynamic_cast<JSDate*>(this_value.as_object())) {
                    out = date;
                    return Completion::empty();
                }
            }

            return evaluator.throw_error(evaluator.type_error_prototype,
                "Date.prototype method called on incompatible receiver");
        }

        JSObject *new_date(Evaluator &evaluator, const double value) {
            auto *date = evaluator.heap.allocate<JSDate>(time_clip(value));
            date->prototype = evaluator.date_prototype;

            return date;
        }

        Completion to_double(Evaluator &evaluator, const JSValue &value, double &out) {
            Completion number = evaluator.to_number(value);
            if (number.is_abrupt()) return number;

            out = number.get_value_or_undefined().to_number();
            return Completion::empty();
        }

        Completion fields_to_time(Evaluator &evaluator, const std::vector<JSValue> &args,
                                  bool adjust_century, double &out) {
            double parts[7] = {0, 0, 1, 0, 0, 0, 0};

            for (std::size_t i = 0; i < 7; ++i) {
                if (i >= args.size()) break;

                if (Completion c = to_double(evaluator, args[i], parts[i]); c.is_abrupt()) return c;
            }

            double year = parts[0];
            if (adjust_century && !std::isnan(year)) {
                const double whole = std::trunc(year);
                if (whole >= 0 && whole <= 99) year = 1900 + whole;
            }

            out = make_date(make_day(year, parts[1], parts[2]),
                            make_time(parts[3], parts[4], parts[5], parts[6]));

            return Completion::empty();
        }

        Completion date_to_string(Evaluator &evaluator, const JSValue &this_value,
                                  const std::vector<JSValue> &args);

        Completion native_date(Evaluator &evaluator, const JSValue&, const std::vector<JSValue>&) {
            JSObject *now = new_date(evaluator, now_milliseconds());

            return date_to_string(evaluator, JSValue::object(now), {});
        }

        Completion construct_date(Evaluator &evaluator, const JSValue&,
                                  const std::vector<JSValue> &args) {
            if (args.empty()) {
                return Completion::normal(JSValue::object(new_date(evaluator, now_milliseconds())));
            }

            if (args.size() == 1) {
                Completion primitive = evaluator.to_primitive(args[0], Hint::Default);
                if (primitive.is_abrupt()) return primitive;

                const JSValue value = primitive.get_value_or_undefined();

                if (value.type() == JSValueType::String) {
                    return Completion::normal(JSValue::object(
                        new_date(evaluator, parse_date(utf16_to_utf8(value.to_u16string())))));
                }

                double number = 0;
                if (Completion c = to_double(evaluator, value, number); c.is_abrupt()) return c;

                return Completion::normal(JSValue::object(new_date(evaluator, number)));
            }

            double value = 0;
            if (Completion c = fields_to_time(evaluator, args, true, value); c.is_abrupt()) return c;

            return Completion::normal(JSValue::object(new_date(evaluator, utc_from_local(value))));
        }

        Completion date_parse(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            Completion text = evaluator.to_string(argument_at(args, 0));
            if (text.is_abrupt()) return text;

            return Completion::normal(JSValue::number(
                parse_date(text.get_value_or_undefined().to_string())));
        }

        Completion date_utc(Evaluator &evaluator, const JSValue&, const std::vector<JSValue> &args) {
            double value = 0;
            if (Completion c = fields_to_time(evaluator, args, true, value); c.is_abrupt()) return c;

            return Completion::normal(JSValue::number(time_clip(value)));
        }

        Completion date_now(Evaluator&, const JSValue&, const std::vector<JSValue>&) {
            return Completion::normal(JSValue::number(now_milliseconds()));
        }

        enum class TextForm { Full, DateOnly, TimeOnly, Utc };

        Completion format(Evaluator &evaluator, const TextForm form, const JSValue &this_value) {
            JSDate *date = nullptr;
            if (Completion c = this_date(evaluator, this_value, date); c.is_abrupt()) return c;

            if (std::isnan(date->primitive_value)) {
                return Completion::normal(JSValue::string("Invalid Date"));
            }

            if (form == TextForm::Utc) {
                const double t = date->primitive_value;

                return Completion::normal(JSValue::string(
                    std::string(WEEK_DAYS[week_day(t)]) + ", " + padded(date_from_time(t), 2) + " "
                    + MONTHS[month_from_time(t)] + " " + signed_year(year_from_time(t)) + " "
                    + clock_part(t) + " GMT"));
            }

            const double t = local_time(date->primitive_value);

            switch (form) {
                case TextForm::DateOnly: return Completion::normal(JSValue::string(date_part(t)));
                case TextForm::TimeOnly:
                    return Completion::normal(JSValue::string(
                        clock_part(t) + " " + zone_part(date->primitive_value)));
                default:
                    return Completion::normal(JSValue::string(
                        date_part(t) + " " + clock_part(t) + " " + zone_part(date->primitive_value)));
            }
        }

        Completion date_to_string(Evaluator &e, const JSValue &t, const std::vector<JSValue>&) {
            return format(e, TextForm::Full, t);
        }
        Completion date_to_date_string(Evaluator &e, const JSValue &t, const std::vector<JSValue>&) {
            return format(e, TextForm::DateOnly, t);
        }
        Completion date_to_time_string(Evaluator &e, const JSValue &t, const std::vector<JSValue>&) {
            return format(e, TextForm::TimeOnly, t);
        }
        Completion date_to_utc_string(Evaluator &e, const JSValue &t, const std::vector<JSValue>&) {
            return format(e, TextForm::Utc, t);
        }

        Completion date_to_iso_string(Evaluator &evaluator, const JSValue &this_value,
                                      const std::vector<JSValue>&) {
            JSDate *date = nullptr;
            if (Completion c = this_date(evaluator, this_value, date); c.is_abrupt()) return c;

            const double t = date->primitive_value;

            if (std::isnan(t)) {
                return evaluator.throw_error(evaluator.range_error_prototype, "Invalid time value");
            }

            const double year = year_from_time(t);
            std::string year_text;

            if (year >= 0 && year <= 9999) {
                year_text = padded(year, 4);
            } else {
                year_text = (year < 0 ? "-" : "+") + padded(std::abs(year), 6);
            }

            return Completion::normal(JSValue::string(
                year_text + "-" + padded(month_from_time(t) + 1, 2) + "-" + padded(date_from_time(t), 2)
                + "T" + clock_part(t) + "." + padded(ms_from_time(t), 3) + "Z"));
        }

        Completion date_to_json(Evaluator &evaluator, const JSValue &this_value,
                                const std::vector<JSValue>&) {
            Completion self = evaluator.to_object(this_value);
            if (self.is_abrupt()) return self;

            JSObject *object = self.get_value_or_undefined().as_object();

            Completion primitive = evaluator.to_primitive(JSValue::object(object), Hint::Number);
            if (primitive.is_abrupt()) return primitive;

            const JSValue value = primitive.get_value_or_undefined();

            if (value.type() == JSValueType::Number && !std::isfinite(value.to_number())) {
                return Completion::normal(JSValue::null());
            }

            Completion method = object->get(evaluator, "toISOString");
            if (method.is_abrupt()) return method;

            const JSValue callback = method.get_value_or_undefined();
            if (callback.type() != JSValueType::Object || !callback.as_object()->is_callable()) {
                return evaluator.throw_error(evaluator.type_error_prototype,
                    "toISOString is not a function");
            }

            return callback.as_object()->call(evaluator, JSValue::object(object), {});
        }


        using FieldReader = double (*)(double);

        Completion read_field(Evaluator &evaluator, const JSValue &this_value,
                              const bool utc, const FieldReader reader) {
            JSDate *date = nullptr;
            if (Completion c = this_date(evaluator, this_value, date); c.is_abrupt()) return c;

            const double t = date->primitive_value;
            if (std::isnan(t)) return Completion::normal(nan_value());

            return Completion::normal(JSValue::number(reader(utc ? t : local_time(t))));
        }

        double read_year(const double t)   { return year_from_time(t); }
        double read_month(const double t)  { return month_from_time(t); }
        double read_date(const double t)   { return date_from_time(t); }
        double read_day(const double t)    { return week_day(t); }
        double read_hour(const double t)   { return hour_from_time(t); }
        double read_minute(const double t) { return min_from_time(t); }
        double read_second(const double t) { return sec_from_time(t); }
        double read_ms(const double t)     { return ms_from_time(t); }

        template <bool Utc, FieldReader Reader>
        Completion getter(Evaluator &e, const JSValue &t, const std::vector<JSValue>&) {
            return read_field(e, t, Utc, Reader);
        }

        Completion date_value_of(Evaluator &evaluator, const JSValue &this_value,
                                 const std::vector<JSValue>&) {
            JSDate *date = nullptr;
            if (Completion c = this_date(evaluator, this_value, date); c.is_abrupt()) return c;

            return Completion::normal(JSValue::number(date->primitive_value));
        }

        Completion date_get_timezone_offset(Evaluator &evaluator, const JSValue &this_value,
                                            const std::vector<JSValue>&) {
            JSDate *date = nullptr;
            if (Completion c = this_date(evaluator, this_value, date); c.is_abrupt()) return c;

            const double t = date->primitive_value;
            if (std::isnan(t)) return Completion::normal(nan_value());

            return Completion::normal(JSValue::number((t - local_time(t)) / MS_PER_MINUTE));
        }

        enum class Field { Year, Month, Date, Hour, Minute, Second, Millisecond };

        Completion write_fields(Evaluator &evaluator, const JSValue &this_value,
                                const std::vector<JSValue> &args,
                                const bool utc, const Field first, const int count) {
            JSDate *date = nullptr;
            if (Completion c = this_date(evaluator, this_value, date); c.is_abrupt()) return c;

            const double stored = date->primitive_value;

            const bool year_form = first == Field::Year;
            if (std::isnan(stored) && !year_form) {
                for (int i = 0; i < count && static_cast<std::size_t>(i) < args.size(); ++i) {
                    double ignored = 0;
                    if (Completion c = to_double(evaluator, args[i], ignored); c.is_abrupt()) return c;
                }

                return Completion::normal(nan_value());
            }

            const double base = std::isnan(stored) ? 0 : (utc ? stored : local_time(stored));

            double parts[7] = {
                year_from_time(base), static_cast<double>(month_from_time(base)), date_from_time(base),
                hour_from_time(base), min_from_time(base), sec_from_time(base), ms_from_time(base)
            };

            const int start = static_cast<int>(first);

            for (std::size_t i = 0; i < args.size(); ++i) {
                double value = 0;
                if (Completion c = to_double(evaluator, args[i], value); c.is_abrupt()) return c;

                if (static_cast<int>(i) < count) parts[start + i] = value;
            }

            const double built = make_date(make_day(parts[0], parts[1], parts[2]),
                                           make_time(parts[3], parts[4], parts[5], parts[6]));

            date->primitive_value = time_clip(utc ? built : utc_from_local(built));

            return Completion::normal(JSValue::number(date->primitive_value));
        }

        template <bool Utc, Field First, int Count>
        Completion setter(Evaluator &e, const JSValue &t, const std::vector<JSValue> &a) {
            return write_fields(e, t, a, Utc, First, Count);
        }

        Completion date_set_time(Evaluator &evaluator, const JSValue &this_value,
                                 const std::vector<JSValue> &args) {
            JSDate *date = nullptr;
            if (Completion c = this_date(evaluator, this_value, date); c.is_abrupt()) return c;

            double value = 0;
            if (Completion c = to_double(evaluator, argument_at(args, 0), value); c.is_abrupt()) return c;

            date->primitive_value = time_clip(value);

            return Completion::normal(JSValue::number(date->primitive_value));
        }

        Completion date_get_year(Evaluator &evaluator, const JSValue &this_value,
                                 const std::vector<JSValue>&) {
            JSDate *date = nullptr;
            if (Completion c = this_date(evaluator, this_value, date); c.is_abrupt()) return c;

            const double t = date->primitive_value;
            if (std::isnan(t)) return Completion::normal(nan_value());

            return Completion::normal(JSValue::number(year_from_time(local_time(t)) - 1900));
        }

        Completion date_set_year(Evaluator &evaluator, const JSValue &this_value,
                                 const std::vector<JSValue> &args) {
            JSDate *date = nullptr;
            if (Completion c = this_date(evaluator, this_value, date); c.is_abrupt()) return c;

            double year = 0;
            if (Completion c = to_double(evaluator, argument_at(args, 0), year); c.is_abrupt()) return c;

            if (std::isnan(year)) {
                date->primitive_value = std::nan("");
                return Completion::normal(nan_value());
            }

            const double whole = std::trunc(year);
            const double full = (whole >= 0 && whole <= 99) ? whole + 1900 : whole;

            const double base = std::isnan(date->primitive_value) ? 0 : local_time(date->primitive_value);

            const double built = make_date(
                make_day(full, month_from_time(base), date_from_time(base)),
                make_time(hour_from_time(base), min_from_time(base),
                          sec_from_time(base), ms_from_time(base)));

            date->primitive_value = time_clip(utc_from_local(built));

            return Completion::normal(JSValue::number(date->primitive_value));
        }
    }

    void install_date(const Builtins &b) {
        JSObject *prototype = b.evaluator.date_prototype;

        JSObject *date_constructor =
            b.constructor("Date", native_date, prototype, construct_date, 7);

        b.method(date_constructor, "parse", date_parse, 1);
        b.method(date_constructor, "UTC",   date_utc, 7);
        b.method(date_constructor, "now",   date_now);

        b.method(prototype, "toString",           date_to_string);
        b.method(prototype, "toDateString",       date_to_date_string);
        b.method(prototype, "toTimeString",       date_to_time_string);
        JSObject *utc_string = b.method(prototype, "toUTCString", date_to_utc_string);
        b.data_property(prototype, "toGMTString", JSValue::object(utc_string));
        b.method(prototype, "toISOString",        date_to_iso_string);
        b.method(prototype, "toJSON",             date_to_json, 1);

        b.method(prototype, "toLocaleString",     date_to_string);
        b.method(prototype, "toLocaleDateString", date_to_date_string);
        b.method(prototype, "toLocaleTimeString", date_to_time_string);

        b.method(prototype, "valueOf",            date_value_of);
        b.method(prototype, "getTime",            date_value_of);
        b.method(prototype, "getTimezoneOffset",  date_get_timezone_offset);

        b.method(prototype, "getFullYear",        getter<false, read_year>);
        b.method(prototype, "getUTCFullYear",     getter<true,  read_year>);
        b.method(prototype, "getMonth",           getter<false, read_month>);
        b.method(prototype, "getUTCMonth",        getter<true,  read_month>);
        b.method(prototype, "getDate",            getter<false, read_date>);
        b.method(prototype, "getUTCDate",         getter<true,  read_date>);
        b.method(prototype, "getDay",             getter<false, read_day>);
        b.method(prototype, "getUTCDay",          getter<true,  read_day>);
        b.method(prototype, "getHours",           getter<false, read_hour>);
        b.method(prototype, "getUTCHours",        getter<true,  read_hour>);
        b.method(prototype, "getMinutes",         getter<false, read_minute>);
        b.method(prototype, "getUTCMinutes",      getter<true,  read_minute>);
        b.method(prototype, "getSeconds",         getter<false, read_second>);
        b.method(prototype, "getUTCSeconds",      getter<true,  read_second>);
        b.method(prototype, "getMilliseconds",    getter<false, read_ms>);
        b.method(prototype, "getUTCMilliseconds", getter<true,  read_ms>);

        b.method(prototype, "setTime",            date_set_time, 1);

        b.method(prototype, "setMilliseconds",    setter<false, Field::Millisecond, 1>, 1);
        b.method(prototype, "setUTCMilliseconds", setter<true,  Field::Millisecond, 1>, 1);
        b.method(prototype, "setSeconds",         setter<false, Field::Second, 2>, 2);
        b.method(prototype, "setUTCSeconds",      setter<true,  Field::Second, 2>, 2);
        b.method(prototype, "setMinutes",         setter<false, Field::Minute, 3>, 3);
        b.method(prototype, "setUTCMinutes",      setter<true,  Field::Minute, 3>, 3);
        b.method(prototype, "setHours",           setter<false, Field::Hour, 4>, 4);
        b.method(prototype, "setUTCHours",        setter<true,  Field::Hour, 4>, 4);
        b.method(prototype, "setDate",            setter<false, Field::Date, 1>, 1);
        b.method(prototype, "setUTCDate",         setter<true,  Field::Date, 1>, 1);
        b.method(prototype, "setMonth",           setter<false, Field::Month, 2>, 2);
        b.method(prototype, "setUTCMonth",        setter<true,  Field::Month, 2>, 2);
        b.method(prototype, "setFullYear",        setter<false, Field::Year, 3>, 3);
        b.method(prototype, "setUTCFullYear",     setter<true,  Field::Year, 3>, 3);

        b.method(prototype, "getYear",            date_get_year);
        b.method(prototype, "setYear",            date_set_year, 1);
    }
}