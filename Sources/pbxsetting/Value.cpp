// Copyright 2013-present Facebook. All Rights Reserved.

#include <pbxsetting/Value.h>

using pbxsetting::Value;

bool Value::Entry::
operator==(Entry const &rhs) const
{
    if (type != rhs.type) {
        return false;
    } else if (type == String) {
        return string == rhs.string;
    } else if (type == Value::Entry::Value) {
        return *value == *rhs.value;
    } else {
        assert(false);
    }
}

Value::
Value(std::vector<Entry> const &entries) :
    _entries(entries)
{
}

Value::
~Value()
{
}

std::string Value::
raw() const
{
    std::string out;
    for (Value::Entry const &entry : _entries) {
        switch (entry.type) {
            case Value::Entry::String: {
                out += entry.string;
                break;
            }
            case Value::Entry::Value: {
                out += "$(" + entry.value->raw() + ")";
                break;
            }
        }
    }
    return out;
}

bool Value::
operator==(Value const &rhs) const
{
    return _entries == rhs._entries;
}

Value Value::
operator+(Value const &rhs) const
{
    std::vector<Value::Entry> entries;
    entries.insert(entries.end(), _entries.begin(), _entries.end());

    auto it = rhs.entries().begin();
    if (!entries.empty() && !rhs.entries().empty()) {
        if (entries.back().type == Value::Entry::String && it->type == Value::Entry::String) {
            entries.back().string += it->string;
            ++it;
        }
    }

    entries.insert(entries.end(), it, rhs.entries().end());
    return Value(entries);
}

struct ParseResult {
    bool found;
    size_t end;
    Value value;
};

enum ValueDelimiter {
    kDelimiterNone,
    kDelimiterParentheses,
    kDelimiterBraces,
    kDelimiterIdentifier,
};

static ParseResult
ParseValue(std::string const &value, size_t from, ValueDelimiter end = kDelimiterNone)
{
    bool found = true;
    std::vector<Value::Entry> entries;

    size_t length;
    size_t search_offset = from;
    size_t append_offset = from;

    do {
        size_t to;
        switch (end) {
            case kDelimiterNone: {
                to = value.size();
                break;
            }
            case kDelimiterParentheses: {
                to = value.find(')', search_offset);
                break;
            }
            case kDelimiterBraces: {
                to = value.find('}', search_offset);
                break;
            }
            case kDelimiterIdentifier: {
                const std::string identifier = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
                to = value.find_first_not_of(identifier, search_offset);
                if (to == std::string::npos) {
                    to = value.size();
                }
                if (to - search_offset == 0) {
                    to = std::string::npos;
                }
                break;
            }
        }
        if (to == std::string::npos) {
            return { .found = false, .end = from, .value = Value(entries) };
        }

        size_t pno = value.find("$(", search_offset);
        size_t cbo = value.find("${", search_offset);
        size_t ido = value.find("$", search_offset);

        size_t openlen = 0;
        size_t open = std::string::npos;
        ValueDelimiter start = kDelimiterNone;
        size_t closelen = 0;
        if (pno != std::string::npos && (pno <= ido || ido == std::string::npos) && (pno < cbo || cbo == std::string::npos)) {
            open = pno;
            openlen = 2;
            start = kDelimiterParentheses;
            closelen = 1;
        } else if (cbo != std::string::npos && (cbo <= ido || ido == std::string::npos) && (cbo < pno || pno == std::string::npos)) {
            open = cbo;
            openlen = 2;
            start = kDelimiterBraces;
            closelen = 1;
        } else if (ido != std::string::npos && (ido < pno || pno == std::string::npos) && (ido < cbo || cbo == std::string::npos)) {
            open = ido;
            openlen = 1;
            start = kDelimiterIdentifier;
            closelen = 0;
        }

        if (open == std::string::npos || start == kDelimiterNone || open >= to) {
            length = to - append_offset;
            if (length > 0) {
                entries.push_back({ .type = Value::Entry::String, .string = value.substr(append_offset, length) });
            }

            return { .found = true, .end = to, .value = Value(entries) };
        }

        ParseResult result = ParseValue(value, open + openlen, start);
        if (result.found) {
            length = open - append_offset;
            if (length > 0) {
                entries.push_back({ .type = Value::Entry::String, .string = value.substr(append_offset, length) });
            }

            entries.push_back({ .type = Value::Entry::Value, .value = std::make_shared<Value>(result.value) });

            append_offset = result.end + closelen;
            search_offset = result.end + closelen;
        } else {
            search_offset += openlen;
        }
    } while (true);
}

Value Value::
Parse(std::string const &value)
{
    return ParseValue(value, 0).value;
}

Value Value::
String(std::string const &value)
{
    if (value.empty()) {
        return Empty();
    }

    return Value({
        Entry({
            .type = Value::Entry::String,
            .string = value,
        }),
    });
}

Value Value::
Variable(std::string const &value)
{
    return Value({
        Entry({
            .type = Value::Entry::Value,
            .value = std::make_shared<Value>(Value({
                Entry({
                    .type = Value::Entry::String,
                    .string = value,
                }),
            })),
        }),
    });
}

Value Value::
FromObject(plist::Object const *object)
{
    if (object == nullptr) {
        return pbxsetting::Value::Empty();
    } else if (plist::String const *stringValue = plist::CastTo <plist::String> (object)) {
        return pbxsetting::Value::Parse(stringValue->value());
    } else if (plist::Boolean const *booleanValue = plist::CastTo <plist::Boolean> (object)) {
        return pbxsetting::Value::Parse(booleanValue->value() ? "YES" : "NO");
    } else if (plist::Integer const *integerValue = plist::CastTo <plist::Integer> (object)) {
        return pbxsetting::Value::Parse(std::to_string(integerValue->value()));
    } else if (plist::Array const *arrayValue = plist::CastTo <plist::Array> (object)) {
        std::string value;
        for (size_t n = 0; n < arrayValue->count(); n++) {
            if (auto arg = arrayValue->value <plist::String> (n)) {
                if (n != 0) {
                    value += " ";
                }
                value += arg->value();
            }
        }
        return pbxsetting::Value::Parse(value);
    } else {
        // TODO(grp): Handle additional types?
        fprintf(stderr, "Warning: Unknown value type for object.\n");
        return pbxsetting::Value::Empty();
    }
}

Value const &Value::
Empty(void)
{
    static Value *value = new Value({ });
    return *value;
}

