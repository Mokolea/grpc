//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef GRPC_SRC_CORE_TRANSPORT_AUTH_CONTEXT_H
#define GRPC_SRC_CORE_TRANSPORT_AUTH_CONTEXT_H

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <stddef.h>

#include <memory>
#include <utility>

#include "absl/strings/string_view.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/surface/connection_context.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/useful.h"

// --- grpc_auth_context ---

// High level authentication context object. Can optionally be chained.

// Property names are always NULL terminated.

struct grpc_auth_property_array {
  grpc_auth_property* array = nullptr;
  size_t count = 0;
  size_t capacity = 0;
};

void grpc_auth_property_reset(grpc_auth_property* property);

#define GRPC_AUTH_CONTEXT_ARG "grpc.auth_context"

// This type is forward declared as a C struct and we cannot define it as a
// class. Otherwise, compiler will complain about type mismatch due to
// -Wmismatched-tags.
// TODO(roth): Consider renaming to something like ConnectionSecurityContext
// to reflect the fact that it stores connection-level security properties.
struct grpc_auth_context
    : public grpc_core::RefCounted<grpc_auth_context,
                                   grpc_core::NonPolymorphicRefCount> {
 public:
  // Base class for all extensions to inherit from.
  class Extension {
   public:
    virtual ~Extension() = default;
  };
  explicit grpc_auth_context(
      grpc_core::RefCountedPtr<grpc_auth_context> chained)
      : grpc_core::RefCounted<grpc_auth_context,
                              grpc_core::NonPolymorphicRefCount>(
            GRPC_TRACE_FLAG_ENABLED(auth_context_refcount)
                ? "auth_context_refcount"
                : nullptr),
        chained_(std::move(chained)) {
    if (chained_ != nullptr) {
      peer_identity_property_name_ = chained_->peer_identity_property_name_;
    }
    connection_context_ = grpc_core::ConnectionContext::Create();
  }

  ~grpc_auth_context() {
    chained_.reset(DEBUG_LOCATION, "chained");
    if (properties_.array != nullptr) {
      for (size_t i = 0; i < properties_.count; i++) {
        grpc_auth_property_reset(&properties_.array[i]);
      }
      gpr_free(properties_.array);
    }
  }

  static absl::string_view ChannelArgName() { return GRPC_AUTH_CONTEXT_ARG; }
  static int ChannelArgsCompare(const grpc_auth_context* a,
                                const grpc_auth_context* b) {
    return QsortCompare(a, b);
  }

  const grpc_auth_context* chained() const { return chained_.get(); }
  const grpc_auth_property_array& properties() const { return properties_; }

  grpc_core::ConnectionContext* connection_context() const {
    return connection_context_.get();
  }

  bool is_authenticated() const {
    return peer_identity_property_name_ != nullptr;
  }
  const char* peer_identity_property_name() const {
    return peer_identity_property_name_;
  }
  void set_peer_identity_property_name(const char* name) {
    peer_identity_property_name_ = name;
  }
  void set_extension(std::unique_ptr<Extension> extension) {
    extension_ = std::move(extension);
  }

  void ensure_capacity();
  void add_property(const char* name, const char* value, size_t value_length);
  void add_cstring_property(const char* name, const char* value);
  void set_protocol(absl::string_view protocol);
  // Returns std::nullopt if auth context comparison is not supported.
  std::optional<bool> CompareAuthContext(const grpc_auth_context* other);

 private:
  grpc_core::RefCountedPtr<grpc_auth_context> chained_;
  grpc_auth_property_array properties_;
  const char* peer_identity_property_name_ = nullptr;
  std::unique_ptr<Extension> extension_;
  grpc_core::OrphanablePtr<grpc_core::ConnectionContext> connection_context_;
  std::string protocol_;
};

// --- Channel args for auth context ---

grpc_arg grpc_auth_context_to_arg(grpc_auth_context* c);
grpc_auth_context* grpc_auth_context_from_arg(const grpc_arg* arg);
grpc_auth_context* grpc_find_auth_context_in_args(
    const grpc_channel_args* args);

#endif  // GRPC_SRC_CORE_TRANSPORT_AUTH_CONTEXT_H
