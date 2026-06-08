#ifndef ZLG_CONFIG_INTF_H
#define ZLG_CONFIG_INTF_H

struct _Meta;
struct _Pair;
struct _Options;
struct _ConfigNode;

typedef struct _Meta Meta;
typedef struct _Pair Pair;
typedef struct _Options Options;
typedef struct _ConfigNode ConfigNode;

struct _Options
{
    const char * type;
    const char * value;
    const char * desc;
};

struct _Meta
{
    const char * type;
    const char * desc;
    int read_only;
    const char * format;
    double min_value;
    double max_value;
    const char * unit;
    double delta;
    const char * visible;
    const char * enable;
    int editable;
    Options **options;
};

struct _Pair
{
    const char * key;
    const char * value;
};

struct _ConfigNode
{
    const char * name;
    const char * value;
    const char * binding_value;
    const char * path;
    Meta *meta_info;
    ConfigNode **children;
    Pair **attributes;
};

typedef const ConfigNode* (*GetPropertysFunc)();
typedef int (*SetValueFunc)(const char* path, const char* value);
typedef const char* (*GetValueFunc)(const char* path);

typedef struct tagIProperty
{
    SetValueFunc SetValue;
    GetValueFunc GetValue;
    GetPropertysFunc GetPropertys;
} IProperty;

#endif /* ZLG_CONFIG_INTF_H */
