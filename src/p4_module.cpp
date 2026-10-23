#include <lua.hpp>

#if defined(_WIN32)
#define P4LUA_EXPORT extern "C" __declspec(dllexport)
#else
#define P4LUA_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#include <string_view>

#include "p4/clientapi.h"
#include "p4/clientprog.h"
#include "p4/enviro.h"
#include "p4/hostenv.h"
#include "p4/ignore.h"
#include "p4/strarray.h"
#include "p4/mapapi.h"
#include "p4/spec.h"
#include "p4/strtable.h"
#include "luaref.h"

namespace {

constexpr char kClientMetatable[] = "p4.client";
StrBuf FormatError(const Error& error, int opts = EF_PLAIN) {
    StrBuf buffer;
    error.Fmt(&buffer, opts);
    int len = buffer.Length();
    const char* text = buffer.Text();
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
        --len;
    }
    buffer.SetLength(len);
    return buffer;
}

bool LuaSpecValue(lua_State* L, int index, StrRef& value) {
    int type = lua_type(L, index);
    if (type != LUA_TSTRING && type != LUA_TNUMBER) {
        return false;
    }
    size_t length = 0;
    const char* text = lua_tolstring(L, index, &length);
    value.Set(const_cast<char*>(text), static_cast<p4size_t>(length));
    return true;
}

bool FillSpecList(lua_State* L, int index, const StrRef& field, StrBufDict& dict, Error* e) {
    size_t count = lua_rawlen(L, index);
    for (size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, index, static_cast<lua_Integer>(i));
        StrRef value;
        if (!LuaSpecValue(L, -1, value)) {
            e->Set(E_FAILED, "Spec input field '%field%' must contain strings") << field;
            lua_pop(L, 1);
            return false;
        }
        dict.SetVar(field, static_cast<int>(i - 1), value);
        lua_pop(L, 1);
    }

    // Reject holes and non-integer keys; Spec::Format assumes a dense list.
    size_t seen = 0;
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        bool valid = lua_isinteger(L, -2);
        if (valid) {
            lua_Integer key = lua_tointeger(L, -2);
            valid = key >= 1 && static_cast<size_t>(key) <= count;
        }
        if (!valid) {
            e->Set(E_FAILED, "Spec input field '%field%' must be a contiguous array") << field;
            lua_pop(L, 2);
            return false;
        }
        ++seen;
        lua_pop(L, 1);
    }
    if (seen != count) {
        e->Set(E_FAILED, "Spec input field '%field%' must be a contiguous array") << field;
        return false;
    }
    return true;
}

bool FormatSpecInput(lua_State* L, int index, StrDict* vars, StrBuf* out, Error* e) {
    out->Clear();
    StrPtr* specdef = vars ? vars->GetVar("specdef") : nullptr;
    if (!specdef) {
        e->Set(E_FAILED, "Spec input requires a server-provided specdef");
        return false;
    }

    Spec spec(specdef->Text(), "", e);
    if (e->Test()) {
        return false;
    }

    StrBufDict dict;
    int base = lua_gettop(L);
    int table = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, table) != 0) {
        if (lua_type(L, -2) != LUA_TSTRING) {
            e->Set(E_FAILED, "Spec input keys must be strings");
            lua_settop(L, base);
            return false;
        }

        size_t keyLength = 0;
        const char* keyText = lua_tolstring(L, -2, &keyLength);
        StrRef field(keyText, static_cast<p4size_t>(keyLength));
        SpecElem* elem = spec.Find(field);
        bool isList = elem && elem->IsList();
        int valueType = lua_type(L, -1);

        if (isList) {
            if (valueType == LUA_TTABLE) {
                if (!FillSpecList(L, lua_gettop(L), field, dict, e)) {
                    lua_settop(L, base);
                    return false;
                }
            } else {
                StrRef value;
                if (!LuaSpecValue(L, -1, value)) {
                    e->Set(E_FAILED, "Spec input field '%field%' must be a string or array") << field;
                    lua_settop(L, base);
                    return false;
                }
                dict.SetVar(field, 0, value);
            }
        } else if (valueType == LUA_TTABLE) {
            if (elem) {
                e->Set(E_FAILED, "Spec input field '%field%' must be a string") << field;
                lua_settop(L, base);
                return false;
            }
            if (!FillSpecList(L, lua_gettop(L), field, dict, e)) {
                lua_settop(L, base);
                return false;
            }
        } else {
            StrRef value;
            if (!LuaSpecValue(L, -1, value)) {
                e->Set(E_FAILED, "Spec input field '%field%' must be a string") << field;
                lua_settop(L, base);
                return false;
            }
            dict.SetVar(field, value);
        }

        lua_pop(L, 1);
    }

    SpecDataTable data(&dict);
    spec.Format(&data, out);
    return !e->Test();
}

void PushErrorMsgId(lua_State* L, Error* error) {
    ErrorId* id = error ? error->GetId(0) : nullptr;
    if (!id) {
        lua_pushnil(L);
        return;
    }
    lua_pushinteger(L, id->UniqueCode());
}

void PushErrorDict(lua_State* L, Error* error) {
    StrDict* dict = error ? error->GetDict() : nullptr;
    if (!dict) {
        lua_pushnil(L);
        return;
    }
    lua_createtable(L, 0, 0);
    for (int index = 0;; ++index) {
        StrRef key;
        StrRef value;
        if (!dict->GetVar(index, key, value)) break;
        lua_pushlstring(L, key.Text(), static_cast<size_t>(key.Length()));
        lua_pushlstring(L, value.Text(), static_cast<size_t>(value.Length()));
        lua_rawset(L, -3);
    }
}

void PushErrorIds(lua_State* L, Error* error) {
    int count = error ? error->GetErrorCount() : 0;
    if (count <= 0) {
        lua_pushnil(L);
        return;
    }
    lua_createtable(L, count, 0);
    for (int index = 0; index < count; ++index) {
        ErrorId* id = error->GetId(index);
        if (!id) continue;
        lua_createtable(L, 0, 8);
        lua_pushinteger(L, id->code); lua_setfield(L, -2, "code");
        lua_pushinteger(L, id->UniqueCode()); lua_setfield(L, -2, "msgid");
        lua_pushinteger(L, id->Generic()); lua_setfield(L, -2, "generic");
        lua_pushinteger(L, id->Severity()); lua_setfield(L, -2, "severity");
        lua_pushinteger(L, id->Subsystem()); lua_setfield(L, -2, "subsystem");
        lua_pushinteger(L, id->SubCode()); lua_setfield(L, -2, "subcode");
        lua_pushinteger(L, id->ArgCount()); lua_setfield(L, -2, "args");
        if (id->fmt) {
            lua_pushstring(L, id->fmt);
        } else {
            lua_pushnil(L);
        }
        lua_setfield(L, -2, "fmt");
        lua_rawseti(L, -2, index + 1);
    }
}

class ResultCollector : public ClientUser {
public:
    explicit ResultCollector(lua_State* L, luaref refMgr, int options_ref = LUA_NOREF)
        : L_(L), refMgr_(refMgr), options_ref_(options_ref) {
        lua_newtable(L);
        ref_ = luaref_ref(refMgr_, L);
        if (ref_ == LUA_NOREF) {
            lua_pop(L, 1);
        }
    }

    ~ResultCollector() override {
        if (ref_ != LUA_NOREF) {
            luaref_unref(refMgr_, ref_);
        }
        if (options_ref_ != LUA_NOREF) {
            luaref_unref(refMgr_, options_ref_);
        }
    }

    int Release() {
        if (ref_ == LUA_NOREF) {
            lua_newtable(L_);
            return 1;
        }
        int ref = ref_;
        ref_ = LUA_NOREF;
        luaref_get(refMgr_, L_, ref);
        luaref_unref(refMgr_, ref);
        return 1;
    }

    void HandleError(Error* error) override {
        HandleMessage(error);
    }

    void Message(Error* error) override {
        HandleMessage(error);
    }

    void OutputError(const char* error_buffer) override {
        // HandleError/Message cover all error paths; OutputError is not called.
    }

    void OutputInfo(char level, const char* data) override {
        EnsureField("info");
        const char* s = data != nullptr ? data : "";
        PushInfo(static_cast<int>(level), s, strlen(s));
        Append();
    }

    void OutputBinary(const char* data, int length) override {
        EnsureField("binary");
        lua_pushlstring(L_, data != nullptr ? data : "", data != nullptr ? static_cast<size_t>(length) : 0);
        Append();
    }

    void OutputText(const char* data, int length) override {
        EnsureField("text");
        lua_pushlstring(L_, data != nullptr ? data : "", data != nullptr ? static_cast<size_t>(length) : 0);
        Append();
    }

    void OutputStat(StrDict* variables) override {
        EnsureField("stats");
        lua_newtable(L_);
        for (int index = 0;; ++index) {
            StrRef key;
            StrRef value;
            if (!variables->GetVar(index, key, value)) break;
            lua_pushlstring(L_, key.Text(), static_cast<size_t>(key.Length()));
            lua_pushlstring(L_, value.Text(), static_cast<size_t>(value.Length()));
            lua_rawset(L_, -3);
        }
        Append();
    }

    void InputData(StrBuf* strbuf, Error* e) override {
        if (options_ref_ != LUA_NOREF) {
            luaref_get(refMgr_, L_, options_ref_);
            int type = lua_getfield(L_, -1, "input");
            if (type == LUA_TSTRING) {
                size_t len = 0;
                const char* s = lua_tolstring(L_, -1, &len);
                strbuf->Set(s, static_cast<p4size_t>(len));
            } else if (type == LUA_TTABLE) {
                FormatSpecInput(L_, -1, varList, strbuf, e);
            }
            lua_pop(L_, 2);
        }
    }

    void Prompt(const StrPtr& msg, StrBuf& rsp, int noEcho, Error* e) override {
        if (options_ref_ != LUA_NOREF) {
            luaref_get(refMgr_, L_, options_ref_);
            if (lua_getfield(L_, -1, "prompt") == LUA_TSTRING) {
                size_t len = 0;
                const char* s = lua_tolstring(L_, -1, &len);
                rsp.Set(s, static_cast<p4size_t>(len));
            }
            lua_pop(L_, 2);
        }
    }

private:
    void HandleMessage(Error* error) {
        auto buf = FormatError(*error);
        size_t length = static_cast<size_t>(buf.Length());
        if (error->IsInfo()) {
            EnsureField("info");
            PushInfo(0, buf.Text(), length);
            Append();
            AppendMessage(error, buf.Text(), length, 0);
        } else if (error->IsWarning()) {
            EnsureField("warnings");
            PushMessage(error, buf.Text(), length);
            Append();
            AppendMessage(error, buf.Text(), length);
        } else {
            EnsureField("errors");
            PushMessage(error, buf.Text(), length);
            Append();
            AppendMessage(error, buf.Text(), length);
        }
    }

    void PushResult() {
        luaref_get(refMgr_, L_, ref_);
    }

    void EnsureField(const char* field) {
        PushResult();
        if (lua_getfield(L_, -1, field) == LUA_TNIL) {
            lua_pop(L_, 1);
            lua_newtable(L_);
            lua_pushvalue(L_, -1);
            lua_setfield(L_, -3, field);
        }
        lua_remove(L_, -2);
    }

    void Append() {
        int len = static_cast<int>(luaL_len(L_, -2));
        lua_rawseti(L_, -2, len + 1);
        lua_pop(L_, 1);
    }

    void PushMessage(Error* error, const char* msg, size_t len, int level = -1) {
        while (len > 0 && msg[len - 1] == '\n') --len;
        lua_createtable(L_, 0, 7);
        lua_pushinteger(L_, error->GetGeneric()); lua_setfield(L_, -2, "code");
        lua_pushinteger(L_, error->GetSeverity()); lua_setfield(L_, -2, "severity");
        lua_pushlstring(L_, msg, len); lua_setfield(L_, -2, "message");
        if (level >= 0) {
            lua_pushinteger(L_, level); lua_setfield(L_, -2, "level");
        }
        PushErrorMsgId(L_, error); lua_setfield(L_, -2, "msgid");
        PushErrorDict(L_, error); lua_setfield(L_, -2, "dict");
        PushErrorIds(L_, error); lua_setfield(L_, -2, "ids");
    }

    void AppendMessage(Error* error, const char* msg, size_t len, int level = -1) {
        EnsureField("messages");
        PushMessage(error, msg, len, level);
        Append();
    }

    void PushInfo(int level, const char* msg, size_t len) {
        lua_createtable(L_, 0, 2);
        lua_pushinteger(L_, level); lua_setfield(L_, -2, "level");
        lua_pushlstring(L_, msg, len); lua_setfield(L_, -2, "data");
    }

    lua_State* L_;
    luaref refMgr_;
    int ref_ = LUA_NOREF;
    int options_ref_ = LUA_NOREF;
};

void UnrefCallback(luaref refMgr, int& ref) {
    if (ref != LUA_NOREF) {
        luaref_unref(refMgr, ref);
        ref = LUA_NOREF;
    }
}

struct ProgressRefs {
    int init = LUA_NOREF;
    int description = LUA_NOREF;
    int total = LUA_NOREF;
    int update = LUA_NOREF;
    int done = LUA_NOREF;
};

void CallLua(lua_State* L, luaref refMgr, int ref, int nargs, int nresults = 0) {
    int base = lua_gettop(L) - nargs;
    luaref_get(refMgr, L, ref);
    lua_insert(L, base + 1);
    if (lua_pcall(L, nargs, nresults, 0) != LUA_OK) {
        lua_pop(L, 1);
        // Keep the stack shape promised by nresults so callers that read a
        // result see nil instead of an unrelated value.
        for (int i = 0; i < nresults; ++i) {
            lua_pushnil(L);
        }
    }
}

class LuaProgress : public ClientProgress {
public:
    LuaProgress(lua_State* L, luaref refMgr, const ProgressRefs& refs, int type)
        : L_(L), refMgr_(refMgr), refs_(refs) {
        if (refs_.init != LUA_NOREF) {
            lua_pushinteger(L_, type);
            CallLua(L_, refMgr_, refs_.init, 1);
        }
    }

    void Description(const StrPtr* desc, int units) override {
        if (refs_.description == LUA_NOREF) return;
        const char* text = desc ? desc->Text() : "";
        size_t length = desc ? static_cast<size_t>(desc->Length()) : 0;
        lua_pushlstring(L_, text, length);
        lua_pushinteger(L_, units);
        CallLua(L_, refMgr_, refs_.description, 2);
    }

    void Total(long total) override {
        if (refs_.total == LUA_NOREF) return;
        lua_pushinteger(L_, total);
        CallLua(L_, refMgr_, refs_.total, 1);
    }

    int Update(long position) override {
        if (refs_.update != LUA_NOREF) {
            lua_pushinteger(L_, position);
            CallLua(L_, refMgr_, refs_.update, 1);
        }
        return 0;
    }

    void Done(int fail) override {
        if (refs_.done == LUA_NOREF) return;
        lua_pushboolean(L_, fail != 0);
        CallLua(L_, refMgr_, refs_.done, 1);
    }

private:
    lua_State* L_;
    luaref refMgr_;
    ProgressRefs refs_;
};

class TagCollector : public ClientUser {
public:
    TagCollector(lua_State* L, luaref refMgr, const ProgressRefs& progress,
                 int on_stat_ref, int on_error_ref,
                 int on_warning_ref, int on_info_ref, int on_text_ref,
                 int on_binary_ref, int on_input_ref, int on_prompt_ref)
        : L_(L), refMgr_(refMgr), progress_(progress),
          on_stat_ref_(on_stat_ref), on_error_ref_(on_error_ref),
          on_warning_ref_(on_warning_ref), on_info_ref_(on_info_ref),
          on_text_ref_(on_text_ref), on_binary_ref_(on_binary_ref),
          on_input_ref_(on_input_ref), on_prompt_ref_(on_prompt_ref) {
    }

    ~TagCollector() override {
        UnrefCallback(refMgr_, on_stat_ref_);
        UnrefCallback(refMgr_, on_error_ref_);
        UnrefCallback(refMgr_, on_warning_ref_);
        UnrefCallback(refMgr_, on_info_ref_);
        UnrefCallback(refMgr_, on_text_ref_);
        UnrefCallback(refMgr_, on_binary_ref_);
        UnrefCallback(refMgr_, on_input_ref_);
        UnrefCallback(refMgr_, on_prompt_ref_);
        UnrefCallback(refMgr_, progress_.init);
        UnrefCallback(refMgr_, progress_.description);
        UnrefCallback(refMgr_, progress_.total);
        UnrefCallback(refMgr_, progress_.update);
        UnrefCallback(refMgr_, progress_.done);
    }

    int ProgressIndicator() override {
        return progress_.init != LUA_NOREF ||
               progress_.description != LUA_NOREF ||
               progress_.total != LUA_NOREF ||
               progress_.update != LUA_NOREF ||
               progress_.done != LUA_NOREF;
    }

    ClientProgress* CreateProgress(int type) override {
        if (!ProgressIndicator()) return nullptr;
        return new LuaProgress(L_, refMgr_, progress_, type);
    }

    int Release(bool ok) {
        lua_pushboolean(L_, ok);
        return 1;
    }

    bool Failed() const { return failed_; }

    void HandleError(Error* error) override {
        HandleMessage(error);
    }

    void Message(Error* error) override {
        HandleMessage(error);
    }

    void OutputError(const char* error_buffer) override {
        // HandleError/Message cover all error paths; OutputError is not called.
    }
    void OutputInfo(char level, const char* data) override {
        if (on_info_ref_ == LUA_NOREF) return;
        const char* s = data ? data : "";
        CallCallback(on_info_ref_, [&]() {
            lua_pushinteger(L_, static_cast<int>(level));
            lua_pushlstring(L_, s, strlen(s));
            lua_pushnil(L_);
            lua_pushnil(L_);
            lua_pushnil(L_);
            lua_pushboolean(L_, 0);
        }, 6);
    }

    void OutputBinary(const char* data, int length) override {
        if (on_binary_ref_ == LUA_NOREF) return;
        CallCallback(on_binary_ref_, [&]() {
            lua_pushlstring(L_, data ? data : "", data ? static_cast<size_t>(length) : 0);
        }, 1);
    }

    void OutputText(const char* data, int length) override {
        if (on_text_ref_ == LUA_NOREF) return;
        CallCallback(on_text_ref_, [&]() {
            lua_pushlstring(L_, data ? data : "", data ? static_cast<size_t>(length) : 0);
        }, 1);
    }

    void OutputStat(StrDict* variables) override {
        if (on_stat_ref_ == LUA_NOREF) return;
        lua_newtable(L_);
        for (int index = 0;; ++index) {
            StrRef key;
            StrRef value;
            if (!variables->GetVar(index, key, value)) break;
            lua_pushlstring(L_, key.Text(), static_cast<size_t>(key.Length()));
            lua_pushlstring(L_, value.Text(), static_cast<size_t>(value.Length()));
            lua_rawset(L_, -3);
        }
        CallCallback(on_stat_ref_, [&]() {}, 1);
    }

    void InputData(StrBuf* strbuf, Error* e) override {
        if (on_input_ref_ == LUA_NOREF) return;
        CallCallback(on_input_ref_, []() {}, 0, 1);
        int type = lua_type(L_, -1);
        if (type == LUA_TSTRING) {
            size_t len = 0;
            const char* s = lua_tolstring(L_, -1, &len);
            strbuf->Set(s, static_cast<p4size_t>(len));
        } else if (type == LUA_TTABLE) {
            FormatSpecInput(L_, -1, varList, strbuf, e);
        }
        lua_pop(L_, 1);
    }

    void Prompt(const StrPtr& msg, StrBuf& rsp, int noEcho, Error* e) override {
        if (on_prompt_ref_ == LUA_NOREF) return;
        lua_pushlstring(L_, msg.Text(), static_cast<size_t>(msg.Length()));
        lua_pushboolean(L_, noEcho);
        CallCallback(on_prompt_ref_, []() {}, 2, 1);
        if (lua_type(L_, -1) == LUA_TSTRING) {
            size_t len = 0;
            const char* s = lua_tolstring(L_, -1, &len);
            rsp.Set(s, static_cast<p4size_t>(len));
        }
        lua_pop(L_, 1);
    }

private:
    void HandleMessage(Error* error) {
        if (error->IsError()) failed_ = true;
        auto buf = FormatError(*error);
        size_t length = static_cast<size_t>(buf.Length());
        if (error->IsInfo()) {
            if (on_info_ref_ == LUA_NOREF) return;
            CallCallback(on_info_ref_, [&]() {
                lua_pushinteger(L_, 0);
                lua_pushlstring(L_, buf.Text(), length);
                PushErrorMsgId(L_, error);
                PushErrorDict(L_, error);
                PushErrorIds(L_, error);
                lua_pushboolean(L_, 1);
            }, 6);
        } else if (error->IsWarning()) {
            if (on_warning_ref_ == LUA_NOREF) return;
            int code = error->GetGeneric();
            CallCallback(on_warning_ref_, [&]() {
                lua_pushinteger(L_, code);
                lua_pushlstring(L_, buf.Text(), length);
                PushErrorMsgId(L_, error);
                PushErrorDict(L_, error);
                PushErrorIds(L_, error);
            }, 5);
        } else {
            if (on_error_ref_ == LUA_NOREF) return;
            int code = error->GetGeneric();
            int severity = error->GetSeverity();
            CallCallback(on_error_ref_, [&]() {
                lua_pushinteger(L_, code);
                lua_pushinteger(L_, severity);
                lua_pushlstring(L_, buf.Text(), length);
                PushErrorMsgId(L_, error);
                PushErrorDict(L_, error);
                PushErrorIds(L_, error);
            }, 6);
        }
    }

    template <typename F>
    void CallCallback(int ref, F push_args, int nargs, int nresults = 0) {
        push_args();
        CallLua(L_, refMgr_, ref, nargs, nresults);
    }

    lua_State* L_;
    luaref refMgr_;
    ProgressRefs progress_;
    int ok_ref_ = LUA_NOREF;
    int on_stat_ref_ = LUA_NOREF;
    int on_error_ref_ = LUA_NOREF;
    int on_warning_ref_ = LUA_NOREF;
    int on_info_ref_ = LUA_NOREF;
    int on_text_ref_ = LUA_NOREF;
    int on_binary_ref_ = LUA_NOREF;
    int on_input_ref_ = LUA_NOREF;
    int on_prompt_ref_ = LUA_NOREF;
    bool failed_ = false;
};

struct OptionDef {
    const char* name;
    void (ClientApi::*set)(const char*);
};

constexpr OptionDef kOptions[] = {
    {"charset",     &ClientApi::SetCharset},
    {"client",      &ClientApi::SetClient},
    {"cwd",         &ClientApi::SetCwd},
    {"enviro_file", &ClientApi::SetEnviroFile},
    {"host",        &ClientApi::SetHost},
    {"ignore_file", &ClientApi::SetIgnoreFile},
    {"language",    &ClientApi::SetLanguage},
    {"password",    &ClientApi::SetPassword},
    {"port",        &ClientApi::SetPort},
    {"prog",        &ClientApi::SetProg},
    {"ticket_file", &ClientApi::SetTicketFile},
    {"trust_file",  &ClientApi::SetTrustFile},
    {"user",        &ClientApi::SetUser},
    {"version",     &ClientApi::SetVersion},
};

class P4Client {
public:
    explicit P4Client(lua_State* L)
        : refMgr_(luaref_init(L)), L_(L) {
        Enviro* enviro = api_.GetEnviro();
        if (enviro) {
            HostEnv henv;
            StrBuf cwd;
            henv.GetCwd(cwd, enviro);
            if (cwd.Length() > 0) {
                enviro->Config(cwd);
            }
        }
        lua_newtable(L);
        settingsRef_ = luaref_ref(refMgr_, L);
    }

    ~P4Client() {
        CloseNoexcept();
        if (refMgr_) {
            luaref_close(refMgr_);
        }
    }

    bool Configure(std::string_view key, std::string_view value, lua_State* L) {
        if (key == "port" && connected_) {
            lua_pushnil(L);
            lua_pushliteral(L, "port must be configured before connect");
            return false;
        }

        if (!ApplyOption(key, value)) {
            lua_pushnil(L);
            lua_pushfstring(L, "unsupported option: %.*s", static_cast<int>(key.size()), key.data());
            return false;
        }
        luaref_get(refMgr_, L_, settingsRef_);
        lua_pushlstring(L_, key.data(), key.size());
        lua_pushlstring(L_, value.data(), value.size());
        lua_rawset(L_, -3);
        lua_pop(L_, 1);
        return true;
    }

    bool PushOption(lua_State* L, std::string_view key) const {
        luaref_get(refMgr_, L, settingsRef_);
        lua_pushlstring(L, key.data(), key.size());
        if (lua_rawget(L, -2) == LUA_TNIL) {
            lua_pop(L, 2);
            return false;
        }
        lua_remove(L, -2);
        return true;
    }

    bool Connect(lua_State* L) {
        return ConnectImpl(L);
    }

    bool Close(lua_State* L) {
        return CloseImpl(L);
    }

    bool Connected() const {
        return connected_;
    }

    luaref GetRefMgr() const {
        return refMgr_;
    }

    char* GetEnv(const char* name) {
        Enviro* enviro = api_.GetEnviro();
        return enviro ? enviro->Get(name) : nullptr;
    }

    const StrPtr& GetConfig() {
        return api_.GetConfig();
    }

    const StrArray* GetConfigs() {
        return api_.GetConfigs();
    }

    void SetProtocol(const char* name, const char* value) {
        api_.SetProtocol(name, value);
    }

    StrPtr* GetProtocol(const char* name) {
        return api_.GetProtocol(name);
    }

    Ignore* GetIgnore() {
        return api_.GetIgnore();
    }

    const StrPtr& GetIgnoreFile() {
        return api_.GetIgnoreFile();
    }

    TagCollector* RunAsync(std::string_view command, char** argv, int argc, lua_State* L,
                           const ProgressRefs& progress,
                           int on_stat_ref, int on_error_ref, int on_warning_ref,
                           int on_info_ref, int on_text_ref, int on_binary_ref,
                           int on_input_ref, int on_prompt_ref) {
        if (connected_ && api_.Dropped()) connected_ = false;
        if (!connected_ && !ConnectImpl(L)) return nullptr;

        api_.SetArgv(argc, argv);
        auto* collector = new TagCollector(L, refMgr_, progress,
            on_stat_ref, on_error_ref, on_warning_ref,
            on_info_ref, on_text_ref, on_binary_ref,
            on_input_ref, on_prompt_ref);
        api_.RunTag(command.data(), collector);
        return collector;
    }

    bool WaitTag(TagCollector* collector) {
        api_.WaitTag(collector);
        const bool dropped = api_.Dropped();
        if (dropped) connected_ = false;
        // ClientApi::GetErrors() counts warnings; only severity >= E_FAILED
        // means the command failed.
        return !collector->Failed() && !dropped;
    }

    bool Run(std::string_view command, char** argv, int argc, lua_State* L, int options_ref = LUA_NOREF) {
        if (command.empty()) {
            lua_pushnil(L);
            lua_pushliteral(L, "command must not be empty");
            return false;
        }

        if (connected_ && api_.Dropped()) {
            connected_ = false;
        }
        if (!connected_ && !ConnectImpl(L)) {
            return false;
        }

        api_.SetArgv(argc, argv);

        ResultCollector collector(L, refMgr_, options_ref);
        api_.Run(command.data(), &collector);

        if (api_.Dropped()) {
            connected_ = false;
        }

        collector.Release();
        return true;
    }

private:
    static const OptionDef* FindOption(std::string_view key) {
        for (const auto& opt : kOptions) {
            if (opt.name == key) return &opt;
        }
        return nullptr;
    }

    bool ApplyOption(std::string_view key, std::string_view value) {
        if (auto* opt = FindOption(key)) {
            (api_.*opt->set)(value.data());
            return true;
        }
        return false;
    }

    bool ConnectImpl(lua_State* L) {
        if (connected_) {
            return true;
        }
        Error p4_error;
        api_.Init(&p4_error);
        if (p4_error.Test()) {
            if (L) {
                auto buf = FormatError(p4_error);
                lua_pushnil(L);
                lua_pushlstring(L, buf.Text(), static_cast<size_t>(buf.Length()));
            }
            return false;
        }
        connected_ = true;
        return true;
    }

    bool CloseImpl(lua_State* L) {
        if (!connected_) {
            return true;
        }
        Error p4_error;
        api_.Final(&p4_error);
        connected_ = false;
        if (p4_error.Test()) {
            if (L) {
                auto buf = FormatError(p4_error);
                lua_pushnil(L);
                lua_pushlstring(L, buf.Text(), static_cast<size_t>(buf.Length()));
            }
            return false;
        }
        return true;
    }

    void CloseNoexcept() noexcept {
        CloseImpl(nullptr);
    }

    ClientApi api_;
    lua_State* L_;
    luaref refMgr_;
    int settingsRef_;
    bool connected_ = false;
};

constexpr char kTagMetatable[] = "p4.tag";

struct P4ClientUserData {
    P4Client* client = nullptr;
};

struct TagUserData {
    TagCollector* collector = nullptr;
    P4Client* client = nullptr;
    bool waited = false;
    bool last_ok = false;
};

P4Client* CheckClient(lua_State* L, int index) {
    auto* userdata = static_cast<P4ClientUserData*>(luaL_checkudata(L, index, kClientMetatable));
    luaL_argcheck(L, userdata->client != nullptr, index, "p4 client is closed");
    return userdata->client;
}

bool IsSupportedOption(std::string_view key) {
    for (const auto& opt : kOptions) {
        if (opt.name == key) return true;
    }
    return false;
}

bool ConfigureFromTable(lua_State* L, int index, P4Client* client) {
    index = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        if (lua_type(L, -2) != LUA_TSTRING) {
            lua_pop(L, 2);
            lua_pushnil(L);
            lua_pushliteral(L, "configuration keys must be strings");
            return false;
        }

        size_t key_length = 0;
        const char* key = lua_tolstring(L, -2, &key_length);
        std::string_view key_view(key, key_length);
        if (!IsSupportedOption(key_view)) {
            lua_pop(L, 2);
            lua_pushnil(L);
            lua_pushfstring(L, "unsupported option: %.*s", static_cast<int>(key_view.size()), key_view.data());
            return false;
        }

        size_t val_len = 0;
        const char* val = lua_tolstring(L, -1, &val_len);
        if (val == nullptr) {
            const char* valtype = luaL_typename(L, -1);
            lua_pop(L, 2);
            lua_pushnil(L);
            lua_pushfstring(L, "option '%.*s' must be a string, got %s",
                            static_cast<int>(key_view.size()), key_view.data(),
                            valtype);
            return false;
        }
        if (!client->Configure(key_view, {val, val_len}, L)) {
            lua_insert(L, -4);  // move error below k:  [table, error, k, v, nil]
            lua_insert(L, -4);  // move nil below error: [table, nil, error, k, v]
            lua_pop(L, 2);      // pop v, k:               [table, nil, error]
            return false;
        }
        lua_pop(L, 1);
    }
    return true;
}

char** ParseArguments(lua_State* L, int index, int* argc) {
    if (lua_isnoneornil(L, index)) {
        *argc = 0;
        return nullptr;
    }

    luaL_checktype(L, index, LUA_TTABLE);
    const lua_Integer count = luaL_len(L, index);
    auto argv = new char*[static_cast<size_t>(count)];
    for (lua_Integer i = 0; i < count; ++i) {
        lua_geti(L, index, i + 1);
        size_t len = 0;
        const char* s = lua_tolstring(L, -1, &len);
        if (s == nullptr) {
            const char* argtype = luaL_typename(L, -1);
            delete[] argv;
            lua_pop(L, 1);
            luaL_error(L, "argument %d must be a string, got %s",
                       static_cast<int>(i + 1), argtype);
            return nullptr;  // unreachable, luaL_error never returns
        }
        argv[i] = const_cast<char*>(s);
        lua_pop(L, 1);
    }
    *argc = static_cast<int>(count);
    return argv;
}

int ClientIndex(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    const char* key = luaL_checkstring(L, 2);

    if (IsSupportedOption(key)) {
        if (!client->PushOption(L, key)) {
            lua_pushnil(L);
        }
        return 1;
    }

    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    return 1;
}

int ClientNewIndex(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    const char* key = luaL_checkstring(L, 2);

    if (IsSupportedOption(key)) {
        size_t val_len = 0;
        const char* val = lua_tolstring(L, 3, &val_len);
        if (val == nullptr) {
            return luaL_error(L, "option '%s' must be a string, got %s",
                              key, luaL_typename(L, 3));
        }
        if (!client->Configure(key, {val, val_len}, L)) {
            return 2;
        }
        return 0;
    }

    return luaL_error(L, "unsupported option: %s", key);
}

int ClientConnect(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    if (!client->Connect(L)) {
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int ClientClose(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    if (!client->Close(L)) {
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int ClientConnected(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    lua_pushboolean(L, client->Connected());
    return 1;
}

int ClientEnv(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    const char* name = luaL_checkstring(L, 2);
    const char* value = client->GetEnv(name);
    if (value == nullptr) {
        lua_pushnil(L);
    } else {
        lua_pushstring(L, value);
    }
    return 1;
}

int ClientP4ConfigFile(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    const StrPtr& config = client->GetConfig();
    if (config.Length() == 0) {
        lua_pushnil(L);
    } else {
        lua_pushlstring(L, config.Text(), static_cast<size_t>(config.Length()));
    }
    return 1;
}

int ClientP4ConfigFiles(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    const StrArray* configs = client->GetConfigs();
    const int count = configs ? configs->Count() : 0;
    lua_createtable(L, count, 0);
    for (int i = 0; i < count; ++i) {
        const StrBuf* entry = configs->Get(i);
        lua_pushlstring(L, entry->Text(), static_cast<size_t>(entry->Length()));
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int ClientProtocol(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    const char* name = luaL_checkstring(L, 2);
    if (!lua_isnoneornil(L, 3)) {
        client->SetProtocol(name, luaL_checkstring(L, 3));
        lua_pushboolean(L, 1);
        return 1;
    }
    StrPtr* value = client->GetProtocol(name);
    if (value == nullptr) {
        lua_pushnil(L);
    } else {
        lua_pushlstring(L, value->Text(), static_cast<size_t>(value->Length()));
    }
    return 1;
}

int ClientIsIgnored(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    size_t length = 0;
    const char* path = luaL_checklstring(L, 2, &length);
    bool ignored = false;
    if (Ignore* ignore = client->GetIgnore()) {
        StrRef ref(path, static_cast<p4size_t>(length));
        ignored = ignore->Reject(ref, client->GetIgnoreFile()) != 0;
    }
    lua_pushboolean(L, ignored);
    return 1;
}

int ClientRun(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    const char* command = luaL_checkstring(L, 2);

    const bool has_options = !lua_isnoneornil(L, 4);
    if (has_options) {
        luaL_checktype(L, 4, LUA_TTABLE);
    }

    int argc = 0;
    char** argv = ParseArguments(L, 3, &argc);

    int options_ref = LUA_NOREF;
    if (has_options) {
        lua_pushvalue(L, 4);
        options_ref = luaref_ref(client->GetRefMgr(), L);
    }

    if (!client->Run(command, argv, argc, L, options_ref)) {
        if (options_ref != LUA_NOREF) {
            luaref_unref(client->GetRefMgr(), options_ref);
        }
        delete[] argv;
        return 2;
    }

    delete[] argv;
    return 1;
}

int ClientGc(lua_State* L) {
    auto* userdata = static_cast<P4ClientUserData*>(luaL_checkudata(L, 1, kClientMetatable));
    delete userdata->client;
    userdata->client = nullptr;
    return 0;
}

TagCollector* CheckTag(lua_State* L, int index) {
    auto* ud = static_cast<TagUserData*>(luaL_checkudata(L, index, kTagMetatable));
    luaL_argcheck(L, ud->collector != nullptr, index, "tag is closed");
    return ud->collector;
}

int TagWait(lua_State* L) {
    auto* ud = static_cast<TagUserData*>(luaL_checkudata(L, 1, kTagMetatable));
    TagCollector* collector = ud->collector;
    P4Client* client = ud->client;
    if (!collector) return 0;
    if (ud->waited) {
        lua_pushboolean(L, ud->last_ok);
        return 1;
    }

    bool ok = client->WaitTag(collector);
    ud->waited = true;
    ud->last_ok = ok;
    return collector->Release(ok);
}

int TagGc(lua_State* L) {
    auto* ud = static_cast<TagUserData*>(luaL_checkudata(L, 1, kTagMetatable));
    if (ud->collector && !ud->waited) {
        // Do not free the collector while RunTag may still be using it.
        ud->client->WaitTag(ud->collector);
        ud->waited = true;
    }
    delete ud->collector;
    ud->collector = nullptr;
    ud->client = nullptr;
    return 0;
}

int ClientRunAsync(lua_State* L) {
    P4Client* client = CheckClient(L, 1);
    luaref refMgr = client->GetRefMgr();
    const char* command = luaL_checkstring(L, 2);

    if (command[0] == '\0') {
        lua_pushnil(L);
        lua_pushliteral(L, "command must not be empty");
        return 2;
    }

    const bool has_callbacks = !lua_isnoneornil(L, 4);
    if (has_callbacks) {
        luaL_checktype(L, 4, LUA_TTABLE);
    }

    int argc = 0;
    char** argv = ParseArguments(L, 3, &argc);

    int on_stat = LUA_NOREF, on_error = LUA_NOREF, on_warn = LUA_NOREF, on_info = LUA_NOREF;
    int on_text = LUA_NOREF, on_bin = LUA_NOREF, on_input = LUA_NOREF, on_prompt = LUA_NOREF;
    ProgressRefs progress;
    if (has_callbacks) {
        auto get_cb = [&](const char* name) -> int {
            if (lua_getfield(L, 4, name) != LUA_TFUNCTION) {
                lua_pop(L, 1);
                return LUA_NOREF;
            }
            int ref = luaref_ref(refMgr, L);
            if (ref == LUA_NOREF) {
                lua_pop(L, 1);
            }
            return ref;
        };
        on_stat   = get_cb("on_stat");
        on_error  = get_cb("on_error");
        on_warn   = get_cb("on_warning");
        on_info   = get_cb("on_info");
        on_text   = get_cb("on_text");
        on_bin    = get_cb("on_binary");
        on_input  = get_cb("on_input");
        on_prompt = get_cb("on_prompt");
        progress.init        = get_cb("on_progress_init");
        progress.description = get_cb("on_progress_description");
        progress.total       = get_cb("on_progress_total");
        progress.update      = get_cb("on_progress_update");
        progress.done        = get_cb("on_progress_done");
    }

    auto* collector = client->RunAsync(command, argv, argc, L, progress,
        on_stat, on_error, on_warn, on_info, on_text, on_bin, on_input, on_prompt);
    delete[] argv;

    if (!collector) {
        UnrefCallback(refMgr, on_stat);
        UnrefCallback(refMgr, on_error);
        UnrefCallback(refMgr, on_warn);
        UnrefCallback(refMgr, on_info);
        UnrefCallback(refMgr, on_text);
        UnrefCallback(refMgr, on_bin);
        UnrefCallback(refMgr, on_input);
        UnrefCallback(refMgr, on_prompt);
        UnrefCallback(refMgr, progress.init);
        UnrefCallback(refMgr, progress.description);
        UnrefCallback(refMgr, progress.total);
        UnrefCallback(refMgr, progress.update);
        UnrefCallback(refMgr, progress.done);
        // ConnectImpl has already pushed (nil, error); return it to the caller.
        return 2;
    }

    auto* ud = static_cast<TagUserData*>(lua_newuserdatauv(L, sizeof(TagUserData), 1));
    ud->collector = collector;
    ud->client = client;
    ud->waited = false;
    ud->last_ok = false;
    lua_pushvalue(L, 1);
    lua_setiuservalue(L, -2, 1);
    if (luaL_newmetatable(L, kTagMetatable)) {
        lua_pushcfunction(L, TagGc);
        lua_setfield(L, -2, "__gc");
        luaL_Reg methods[] = {
            {"wait", TagWait},
            {nullptr, nullptr},
        };
        luaL_newlib(L, methods);
        lua_setfield(L, -2, "__index");
    }
    lua_setmetatable(L, -2);
    return 1;
}

constexpr char kMapMetatable[] = "p4.map";

struct P4MapUserData {
    MapApi* map = nullptr;
};

MapApi* CheckMap(lua_State* L, int index) {
    auto* ud = static_cast<P4MapUserData*>(luaL_checkudata(L, index, kMapMetatable));
    luaL_argcheck(L, ud->map != nullptr, index, "p4 map is closed");
    return ud->map;
}

MapDir GetDirection(lua_State* L, int index) {
    lua_Integer direction = luaL_optinteger(L, index, MapLeftRight);
    luaL_argcheck(L, direction == MapLeftRight || direction == MapRightLeft,
                  index, "invalid map direction");
    return static_cast<MapDir>(direction);
}

P4MapUserData* NewMapUserdata(lua_State* L, MapApi* map);

int MapInsert(lua_State* L);
int MapTranslate(lua_State* L);
int MapTranslateArray(lua_State* L);
int MapReverse(lua_State* L);
int MapJoin(lua_State* L);
int MapEntries(lua_State* L);
int MapCount(lua_State* L);
int MapClear(lua_State* L);

int MapGc(lua_State* L) {
    auto* ud = static_cast<P4MapUserData*>(luaL_checkudata(L, 1, kMapMetatable));
    delete ud->map;
    ud->map = nullptr;
    return 0;
}

int MapInsert(lua_State* L) {
    MapApi* map = CheckMap(L, 1);
    size_t leftLength = 0;
    size_t rightLength = 0;
    const char* left = luaL_checklstring(L, 2, &leftLength);
    const char* right = luaL_checklstring(L, 3, &rightLength);
    lua_Integer type = luaL_optinteger(L, 4, MapInclude);
    luaL_argcheck(L, type >= MapInclude && type <= MapOneToMany, 4, "invalid map type");
    map->Insert(StrRef(left, static_cast<p4size_t>(leftLength)),
                StrRef(right, static_cast<p4size_t>(rightLength)),
                static_cast<MapType>(type));
    lua_pushboolean(L, 1);
    return 1;
}

int MapTranslate(lua_State* L) {
    MapApi* map = CheckMap(L, 1);
    size_t length = 0;
    const char* text = luaL_checklstring(L, 2, &length);
    StrRef from(text, static_cast<p4size_t>(length));
    StrBuf to;
    if (map->Translate(from, to, GetDirection(L, 3))) {
        lua_pushlstring(L, to.Text(), to.Length());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int MapTranslateArray(lua_State* L) {
    MapApi* map = CheckMap(L, 1);
    size_t length = 0;
    const char* text = luaL_checklstring(L, 2, &length);
    StrRef from(text, static_cast<p4size_t>(length));
    StrArray to;
    if (!map->Translate(from, to, GetDirection(L, 3))) {
        lua_pushnil(L);
        return 1;
    }
    const int count = to.Count();
    lua_createtable(L, count, 0);
    for (int i = 0; i < count; ++i) {
        const StrBuf* value = to.Get(i);
        lua_pushlstring(L, value->Text(), value->Length());
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int MapReverse(lua_State* L) {
    MapApi* map = CheckMap(L, 1);
    auto* reversed = new MapApi;
    for (int i = 0; i < map->Count(); ++i) {
        reversed->Insert(*map->GetRight(i), *map->GetLeft(i), map->GetType(i));
    }
    NewMapUserdata(L, reversed);
    return 1;
}

int MapJoin(lua_State* L) {
    MapApi* left = CheckMap(L, 1);
    MapApi* right = CheckMap(L, 2);
    MapApi* joined = MapApi::Join(left, right);
    if (!joined) {
        lua_pushnil(L);
        lua_pushliteral(L, "failed to join maps");
        return 2;
    }
    NewMapUserdata(L, joined);
    return 1;
}

int MapEntries(lua_State* L) {
    MapApi* map = CheckMap(L, 1);
    const int count = map->Count();
    lua_createtable(L, count, 0);
    for (int i = 0; i < count; ++i) {
        const StrPtr* left = map->GetLeft(i);
        const StrPtr* right = map->GetRight(i);
        lua_createtable(L, 0, 3);
        lua_pushlstring(L, left->Text(), left->Length());
        lua_setfield(L, -2, "left");
        lua_pushlstring(L, right->Text(), right->Length());
        lua_setfield(L, -2, "right");
        lua_pushinteger(L, map->GetType(i));
        lua_setfield(L, -2, "type");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int MapCount(lua_State* L) {
    lua_pushinteger(L, CheckMap(L, 1)->Count());
    return 1;
}

int MapClear(lua_State* L) {
    CheckMap(L, 1)->Clear();
    return 0;
}

P4MapUserData* NewMapUserdata(lua_State* L, MapApi* map) {
    auto* ud = static_cast<P4MapUserData*>(lua_newuserdatauv(L, sizeof(P4MapUserData), 0));
    ud->map = map;
    if (luaL_newmetatable(L, kMapMetatable)) {
        lua_pushcfunction(L, MapGc);
        lua_setfield(L, -2, "__gc");
        luaL_Reg methods[] = {
            {"insert", MapInsert},
            {"translate", MapTranslate},
            {"translate_array", MapTranslateArray},
            {"reverse", MapReverse},
            {"join", MapJoin},
            {"entries", MapEntries},
            {"count", MapCount},
            {"clear", MapClear},
            {nullptr, nullptr},
        };
        luaL_newlib(L, methods);
        lua_setfield(L, -2, "__index");
    }
    lua_setmetatable(L, -2);
    return ud;
}

int ModuleMap(lua_State* L) {
    if (lua_gettop(L) != 0) {
        return luaL_error(L, "p4.map expects no arguments");
    }
    NewMapUserdata(L, new MapApi());
    return 1;
}

int ModuleNew(lua_State* L) {
    auto* userdata = static_cast<P4ClientUserData*>(lua_newuserdatauv(L, sizeof(P4ClientUserData), 0));
    userdata->client = nullptr;

    try {
        userdata->client = new P4Client(L);
    } catch (...) {
        return luaL_error(L, "failed to allocate p4 client");
    }

    if (luaL_newmetatable(L, kClientMetatable)) {
        lua_pushcfunction(L, ClientGc);
        lua_setfield(L, -2, "__gc");
        lua_pushcfunction(L, ClientClose);
        lua_setfield(L, -2, "__close");

        luaL_Reg methods[] = {
            {"close", ClientClose},
            {"connect", ClientConnect},
            {"connected", ClientConnected},
            {"env", ClientEnv},
            {"is_ignored", ClientIsIgnored},
            {"p4config_file", ClientP4ConfigFile},
            {"p4config_files", ClientP4ConfigFiles},
            {"protocol", ClientProtocol},
            {"run", ClientRun},
            {"run_async", ClientRunAsync},
            {nullptr, nullptr},
        };
        luaL_newlib(L, methods);

        lua_pushvalue(L, -1);
        lua_pushcclosure(L, ClientIndex, 1);
        lua_setfield(L, -3, "__index");

        lua_pushcfunction(L, ClientNewIndex);
        lua_setfield(L, -3, "__newindex");

        lua_pop(L, 1);
    }
    lua_setmetatable(L, -2);

    if (!lua_isnoneornil(L, 1)) {
        luaL_checktype(L, 1, LUA_TTABLE);
        if (!ConfigureFromTable(L, 1, userdata->client)) {
            delete userdata->client;
            userdata->client = nullptr;
            return 2;
        }
    }

    return 1;
}

} // namespace

P4LUA_EXPORT int luaopen_p4(lua_State* L) {
    luaL_Reg libs[] = {
        {"new", ModuleNew},
        {"map", ModuleMap},
        {nullptr, nullptr},
    };
    luaL_newlib(L, libs);
    lua_pushinteger(L, MapInclude);
    lua_setfield(L, -2, "MAP_INCLUDE");
    lua_pushinteger(L, MapExclude);
    lua_setfield(L, -2, "MAP_EXCLUDE");
    lua_pushinteger(L, MapOverlay);
    lua_setfield(L, -2, "MAP_OVERLAY");
    lua_pushinteger(L, MapOneToMany);
    lua_setfield(L, -2, "MAP_ONE_TO_MANY");
    lua_pushinteger(L, MapLeftRight);
    lua_setfield(L, -2, "MAP_LEFT_RIGHT");
    lua_pushinteger(L, MapRightLeft);
    lua_setfield(L, -2, "MAP_RIGHT_LEFT");
    return 1;
}
