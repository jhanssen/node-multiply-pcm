#include <nan.h>
#include <uv.h>
#include <node.h>
#include <node_buffer.h>

struct Data
{
    Data();
    ~Data();

    static Nan::Persistent<v8::Private> extName;

    Nan::Persistent<v8::Context> context;
    Nan::Persistent<v8::Function> callback;
    Nan::Persistent<v8::Object> weak;
    Nan::Persistent<v8::Value> persistentData;
    v8::Isolate* isolate;
    float multiply;
    bool working, collected;
    uv_work_t work;

    struct Format
    {
        int32_t channels;
        int32_t bitsPerSample;
        int32_t sampleRate;
        bool isSigned;
    };
    std::vector<Format> formats;

    struct
    {
        void* data;
        size_t size;
    } buffer;

    void perform();
    void complete();

    static void weakCallback(const Nan::WeakCallbackInfo<Data> &data);
};

Nan::Persistent<v8::Private> Data::extName;

Data::Data()
    : isolate(0), working(false), collected(false)
{
}

Data::~Data()
{
}

template<uint8_t Increment, bool Signed, typename Type>
struct Performer
{
    void perform(Data* data, int iterations)
    {
        const float multiply = data->multiply;
        Type* type = static_cast<Type*>(data->buffer.data);
        for (int i = 0; i < iterations; ++i, ++type) {
            *type = *type * multiply;
        }
    }
};

// specialization for signed 24
template<typename Type>
struct Performer<24, true, Type>
{
    void perform(Data* data, int iterations)
    {
        union {
            uint8_t data8[4];
            int32_t data32;
        };
        const float multiply = data->multiply;
        // this code will only work if both the buffer and our cpu is little endian
        uint8_t* type = static_cast<uint8_t*>(data->buffer.data);
        uint8_t flag;
        for (int i = 0; i < iterations; ++i, type += 3) {
            // make an int32_t out of our 3 bytes
            flag = type[2] & 0x80;

            data8[0] = type[0];
            data8[1] = type[1];
            data8[2] = type[2] & 0x7f;
            data8[3] = flag;

            // multiply and mask
            data32 *= multiply;
            data32 &= 0x7fffff;

            // pack our int32_t back into the 3 bufffer bytes
            type[0] = data8[0];
            type[1] = data8[1];
            type[2] = data8[2] | flag;
        }
    }
};

// specialization for unsigned 24
template<typename Type>
struct Performer<24, false, Type>
{
    void perform(Data* data, int iterations)
    {
        union {
            uint8_t data8[4];
            uint32_t data32;
        };
        const float multiply = data->multiply;
        // this code will only work if both the buffer and our cpu is little endian
        uint8_t* type = static_cast<uint8_t*>(data->buffer.data);
        for (int i = 0; i < iterations; ++i, type += 3) {
            // make an int32_t out of our 3 bytes
            data8[0] = type[0];
            data8[1] = type[1];
            data8[2] = type[2];
            data8[3] = 0;

            // multiply and mask
            data32 *= multiply;
            data32 &= 0xffffff;

            // pack our int32_t back into the 3 bufffer bytes
            type[0] = data8[0];
            type[1] = data8[1];
            type[2] = data8[2];
        }
    }
};

void Data::perform()
{
    const Format format = formats.front();
    // multiply buffer with our current format

    const int bytesPerSample = format.bitsPerSample >> 3;
    const int iterations = buffer.size / bytesPerSample;// / format.channels;
    const bool isSigned = format.isSigned;

    switch (format.bitsPerSample) {
    case 8: {
        if (isSigned) {
            Performer<8, true, int8_t> performer;
            performer.perform(this, iterations);
        } else {
            Performer<8, false, uint8_t> performer;
            performer.perform(this, iterations);
        }
        break; }
    case 16: {
        if (isSigned) {
            Performer<16, true, int16_t> performer;
            performer.perform(this, iterations);
        } else {
            Performer<16, false, uint16_t> performer;
            performer.perform(this, iterations);
        }
        break; }
    case 24: {
        if (isSigned) {
            Performer<24, true, int32_t> performer;
            performer.perform(this, iterations);
        } else {
            Performer<24, false, uint32_t> performer;
            performer.perform(this, iterations);
        }
        break; }
    case 32: {
        if (isSigned) {
            Performer<32, true, int32_t> performer;
            performer.perform(this, iterations);
        } else {
            Performer<32, false, uint32_t> performer;
            performer.perform(this, iterations);
        }
        break; }
    }
}

void Data::complete()
{
    if (collected) {
        delete this;
        return;
    }

    Nan::HandleScope scope;

    persistentData.Reset();

    working = false;
    if (formats.size() > 1) {
        // we want the last format to be our only format
        const Format f = formats.back();
        formats.clear();
        formats.push_back(f);
    }
    auto ctx = Nan::GetCurrentContext();
    v8::Local<v8::Function> cb = v8::Local<v8::Function>::New(isolate, callback);
    if (cb->Call(ctx, cb, 0, nullptr).IsEmpty()) {
        Nan::ThrowError("Failed to call");
    }
}

void Data::weakCallback(const Nan::WeakCallbackInfo<Data> &data)
{
    Data* param = data.GetParameter();
    if (param->working) {
        param->collected = true;
    } else {
        delete param;
    }
}

NAN_METHOD(New) {
    if (!info[0]->IsFunction()) {
        Nan::ThrowError("Needs a callback argument");
        return;
    }
    if (!info[1]->IsNumber()) {
        Nan::ThrowError("Needs a multiply argument");
        return;
    }

    auto ctx = Nan::GetCurrentContext();

    Data* data = new Data;
    data->isolate = info.GetIsolate();
    data->callback.Reset(v8::Local<v8::Function>::Cast(info[0]));
    data->multiply = v8::Local<v8::Number>::Cast(info[1])->Value();

    data->work.data = data;

    v8::Local<v8::External> ext = v8::External::New(data->isolate, data);
    v8::Local<v8::Object> weak = v8::Object::New(data->isolate);
    v8::Local<v8::Private> extName;
    if (Data::extName.IsEmpty()) {
        extName = v8::Private::New(data->isolate, v8::String::NewFromUtf8(data->isolate, "ext"));
        Data::extName.Reset(extName);
    } else {
        extName = v8::Local<v8::Private>::New(data->isolate, Data::extName);
    }
    weak->SetPrivate(ctx, extName, ext);
    data->weak.Reset(weak);
    data->weak.SetWeak(data, Data::weakCallback, Nan::WeakCallbackType::kParameter);
    info.GetReturnValue().Set(weak);
}

NAN_METHOD(SetFormat) {
    if (info.Length() < 5) {
        Nan::ThrowError("Need at least five arguments");
        return;
    }
    if (!info[0]->IsObject()) {
        Nan::ThrowError("First argument must be an object");
        return;
    }

    auto iso = info.GetIsolate();
    auto ctx = Nan::GetCurrentContext();
    v8::Local<v8::Object> obj = v8::Local<v8::Object>::Cast(info[0]);
    v8::Local<v8::Private> extName = v8::Local<v8::Private>::New(iso, Data::extName);
    if (!obj->HasPrivate(ctx, extName).ToChecked()) {
        Nan::ThrowError("Argument must have an external");
        return;
    }
    v8::Local<v8::Value> extValue = obj->GetPrivate(ctx, extName).ToLocalChecked();
    Data* data = static_cast<Data*>(v8::Local<v8::External>::Cast(extValue)->Value());
    data->formats.push_back(Data::Format{ info[1]->Int32Value(), info[2]->Int32Value(), info[3]->Int32Value(), info[4]->BooleanValue() });
}

NAN_METHOD(Feed) {
    if (!info[0]->IsObject()) {
        Nan::ThrowError("Argument must be an object");
        return;
    }

    auto iso = info.GetIsolate();
    auto ctx = Nan::GetCurrentContext();
    v8::Local<v8::Object> obj = v8::Local<v8::Object>::Cast(info[0]);
    v8::Local<v8::Private> extName = v8::Local<v8::Private>::New(iso, Data::extName);
    if (!obj->HasPrivate(ctx, extName).ToChecked()) {
        Nan::ThrowError("Argument must have an external");
        return;
    }
    v8::Local<v8::Value> extValue = obj->GetPrivate(ctx, extName).ToLocalChecked();
    Data* data = static_cast<Data*>(v8::Local<v8::External>::Cast(extValue)->Value());
    if (data->working) {
        Nan::ThrowError("Already doing work");
        return;
    }
    if (data->formats.empty()) {
        Nan::ThrowError("No format set");
        return;
    }

    char* dt = node::Buffer::Data(info[1]);
    const size_t size = node::Buffer::Length(info[1]);

    if (!size)
        return;

    if (data->multiply == 1.) {
        data->complete();
    }

    data->buffer.data = dt;
    data->buffer.size = size;
    data->persistentData.Reset(info[1]);

    data->working = true;
    uv_queue_work(uv_default_loop(),
                  &data->work,
                  [](uv_work_t* work) {
                      static_cast<Data*>(work->data)->perform();
                  },
                  [](uv_work_t* work, int) {
                      static_cast<Data*>(work->data)->complete();
                  });
}

NAN_MODULE_INIT(Initialize) {
    NAN_EXPORT(target, New);
    NAN_EXPORT(target, SetFormat);
    NAN_EXPORT(target, Feed);
}

NODE_MODULE(multiply, Initialize)
