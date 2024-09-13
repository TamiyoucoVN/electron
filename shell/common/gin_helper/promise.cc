// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <string_view>

#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "shell/common/gin_helper/microtasks_scope.h"
#include "shell/common/gin_helper/promise.h"
#include "shell/common/process_util.h"
#include "v8/include/v8-context.h"

namespace gin_helper {

namespace {

struct SettleScope {
  explicit SettleScope(const PromiseBase& base)
      : handle_scope_{base.isolate()},
        context_{base.GetContext()},
        microtasks_scope_{base.isolate(), context_->GetMicrotaskQueue(), false,
                          v8::MicrotasksScope::kRunMicrotasks},
        context_scope_{context_} {}

  v8::HandleScope handle_scope_;
  v8::Local<v8::Context> context_;
  // Promise resolution is a microtask
  // We use the MicrotasksRunner to trigger the running of pending microtasks
  gin_helper::MicrotasksScope microtasks_scope_;
  v8::Context::Scope context_scope_;
};

}  // namespace

PromiseBase::PromiseBase(v8::Isolate* isolate)
    : PromiseBase(isolate,
                  v8::Promise::Resolver::New(isolate->GetCurrentContext())
                      .ToLocalChecked()) {}

PromiseBase::PromiseBase(v8::Isolate* isolate,
                         v8::Local<v8::Promise::Resolver> handle)
    : isolate_(isolate),
      context_(isolate, isolate->GetCurrentContext()),
      resolver_(isolate, handle) {}

PromiseBase::PromiseBase() : isolate_(nullptr) {}

PromiseBase::PromiseBase(PromiseBase&&) = default;

PromiseBase::~PromiseBase() = default;

PromiseBase& PromiseBase::operator=(PromiseBase&&) = default;

// static
scoped_refptr<base::TaskRunner> PromiseBase::GetTaskRunner() {
  if (electron::IsBrowserProcess() &&
      !content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    return content::GetUIThreadTaskRunner({});
  }
  return {};
}

v8::Maybe<bool> PromiseBase::Reject(v8::Local<v8::Value> except) {
  SettleScope settle_scope{*this};
  return GetInner()->Reject(settle_scope.context_, except);
}

v8::Maybe<bool> PromiseBase::Reject() {
  v8::HandleScope handle_scope(isolate());
  return Reject(v8::Undefined(isolate()));
}

v8::Maybe<bool> PromiseBase::RejectWithErrorMessage(std::string_view message) {
  v8::HandleScope handle_scope(isolate());
  return Reject(v8::Exception::Error(gin::StringToV8(isolate(), message)));
}

v8::Maybe<bool> PromiseBase::Resolve(v8::Local<v8::Value> value) {
  SettleScope settle_scope{*this};
  return GetInner()->Resolve(settle_scope.context_, value);
}

v8::Local<v8::Context> PromiseBase::GetContext() const {
  return v8::Local<v8::Context>::New(isolate_, context_);
}

v8::Local<v8::Promise> PromiseBase::GetHandle() const {
  return GetInner()->GetPromise();
}

v8::Local<v8::Promise::Resolver> PromiseBase::GetInner() const {
  return resolver_.Get(isolate());
}

// static
void Promise<void>::ResolvePromise(Promise<void> promise) {
  if (auto task_runner = GetTaskRunner()) {
    task_runner->PostTask(
        FROM_HERE,
        base::BindOnce([](Promise<void> promise) { promise.Resolve(); },
                       std::move(promise)));
  } else {
    promise.Resolve();
  }
}

// static
v8::Local<v8::Promise> Promise<void>::ResolvedPromise(v8::Isolate* isolate) {
  Promise<void> resolved(isolate);
  resolved.Resolve();
  return resolved.GetHandle();
}

v8::Maybe<bool> Promise<void>::Resolve() {
  v8::HandleScope handle_scope(isolate());
  return PromiseBase::Resolve(v8::Undefined(isolate()));
}

}  // namespace gin_helper
