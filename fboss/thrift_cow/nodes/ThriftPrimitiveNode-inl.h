/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

// TODO: replace with our own ThriftTraverseResult
#include <folly/json/dynamic.h>
#include <thrift/lib/cpp2/folly_dynamic/folly_dynamic.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <functional>
#include "fboss/agent/state/NodeBase-defs.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_oper_types.h"
#include "fboss/thrift_cow/nodes/NodeUtils.h"
#include "fboss/thrift_cow/nodes/Serializer.h"
#include "fboss/thrift_cow/visitors/PathVisitor.h"

namespace facebook::fboss::thrift_cow {

template <typename TypeClass, typename TType, bool Immutable>
class ThriftPrimitiveNode : public thrift_cow::Serializable {
 public:
  using Self = ThriftPrimitiveNode<TypeClass, TType, Immutable>;
  using CowType = FieldsType;
  using TC = TypeClass;
  using ThriftType = TType;
  using PathIter = typename std::vector<std::string>::const_iterator;

  static constexpr bool immutable = Immutable;

  ThriftPrimitiveNode() = default;
  explicit ThriftPrimitiveNode(ThriftType obj) : obj_(std::move(obj)) {}

  template <typename T = Self>
  void set(const ThriftType& obj)
    requires(!T::immutable)
  {
    obj_ = obj;
  }

  template <typename T = Self>
  void set(const ThriftType&) const
    requires(T::immutable)
  {
    throwImmutableException();
  }

  void operator=(const ThriftType& obj) {
    if constexpr (!Immutable) {
      set(obj);
    } else {
      throwImmutableException();
    }
  }

  void operator==(const Self& other) {
    return obj_ == other.obj_;
  }

  void operator!=(const Self& other) {
    return obj_ != other.obj_;
  }

  void operator<(const Self& other) {
    return obj_ < other.obj_;
  }

  ThriftType operator*() const {
    return obj_;
  }

  ThriftType toThrift() const {
    return obj_;
  }

  template <typename T = Self>
  void fromThrift(const ThriftType& obj)
    requires(!T::immutable)
  {
    set(obj);
  }

  template <typename T = Self>
  void fromThrift(const ThriftType&) const
    requires(T::immutable)
  {
    throwImmutableException();
  }

#ifdef ENABLE_DYNAMIC_APIS

  virtual folly::dynamic toFollyDynamic() const override {
    folly::dynamic out;
    if constexpr (std::is_same_v<ThriftType, folly::basic_fbstring<char>>) {
      out = folly::dynamic(obj_.c_str());
    } else {
      facebook::thrift::to_dynamic(
          out, toThrift(), facebook::thrift::dynamic_format::JSON_1);
    }
    return out;
  }

  template <typename T = Self>
  void fromFollyDynamic(const folly::dynamic& value)
    requires(!T::immutable)
  {
    ThriftType thrift;
    facebook::thrift::from_dynamic(
        thrift, value, facebook::thrift::dynamic_format::JSON_1);
    fromThrift(thrift);
  }

  template <typename T = Self>
  void fromFollyDynamic(const folly::dynamic&) const
    requires(T::immutable)
  {
    throwImmutableException();
  }
#else
  virtual folly::dynamic toFollyDynamic() const override {
    return {};
  }
#endif

  folly::IOBuf encodeBuf(fsdb::OperProtocol proto) const override {
    return serializeBuf<TypeClass>(proto, toThrift());
  }

  void fromEncodedBuf(fsdb::OperProtocol proto, folly::IOBuf&& encoded)
      override {
    if constexpr (immutable) {
      throwImmutableException();
    } else {
      fromThrift(deserializeBuf<TypeClass, TType>(proto, std::move(encoded)));
    }
  }

  void fromEncodedBuf(fsdb::OperProtocol proto, folly::IOBuf&& encoded) const {
    throwImmutableException();
  }

  virtual bool remove(const std::string& token) override {
    throw std::runtime_error(folly::to<std::string>(
        "Cannot remove a child from a primitive node: ", token));
  }

  bool remove(const std::string& token) const {
    throw std::runtime_error(folly::to<std::string>(
        "Cannot remove a child from a primitive node: ", token));
  }

  virtual void modify(const std::string&, bool = true) override {
    if constexpr (immutable) {
      throwImmutableException();
    }
  }

  void modify(const std::string&, bool = true) const {
    throwImmutableException();
  }

  ThriftType& ref() {
    return obj_;
  }

  const ThriftType& ref() const {
    return obj_;
  }

  const ThriftType& cref() const {
    return obj_;
  }

  std::size_t hash() const {
    return std::hash<ThriftType>()(obj_);
  }

 private:
  [[noreturn]] void throwImmutableException() const {
    throw NodeException(
        NodeException::Reason::SET_IMMUTABLE_PRIMITIVE_NODE,
        "Cannot mutate an immutable primitive node");
  }

  ThriftType obj_{};
};

template <typename TC, typename TType>
using ImmutableThriftPrimitiveNode = ThriftPrimitiveNode<TC, TType, true>;

template <typename PrimitiveNode>
bool operator==(
    const PrimitiveNode& node,
    const typename PrimitiveNode::ThriftType& data) {
  return node.cref() == data;
}

template <typename PrimitiveNode>
bool operator==(
    const typename PrimitiveNode::ThriftType& data,
    const PrimitiveNode& node) {
  return node.cref() == data;
}

template <typename TC, typename TType, bool Immutable>
bool operator==(
    const ThriftPrimitiveNode<TC, TType, Immutable>& nodeA,
    const ThriftPrimitiveNode<TC, TType, Immutable>& nodeB) {
  return nodeA.cref() == nodeB.cref();
}

template <typename TC, typename TType, bool Immutable>
bool operator==(
    const std::optional<ThriftPrimitiveNode<TC, TType, Immutable>>& nodeA,
    const std::optional<ThriftPrimitiveNode<TC, TType, Immutable>>& nodeB) {
  if (nodeA.has_value() && nodeB.has_value()) {
    return *nodeA == *nodeB;
  } else if (nodeA.has_value() || nodeB.has_value()) {
    return false;
  }
  return true;
}

template <typename PrimitiveNode>
bool operator!=(
    const PrimitiveNode& node,
    const typename PrimitiveNode::ThriftType& data) {
  return node.cref() != data;
}

template <typename PrimitiveNode>
bool operator!=(
    const typename PrimitiveNode::ThriftType& data,
    const PrimitiveNode& node) {
  return node.cref() != data;
}

template <typename TC, typename TType, bool Immutable>
bool operator!=(
    const ThriftPrimitiveNode<TC, TType, Immutable>& nodeA,
    const ThriftPrimitiveNode<TC, TType, Immutable>& nodeB) {
  return nodeA.cref() != nodeB.cref();
}

template <typename TC, typename TType, bool Immutable>
bool operator!=(
    const std::optional<ThriftPrimitiveNode<TC, TType, Immutable>>& nodeA,
    const std::optional<ThriftPrimitiveNode<TC, TType, Immutable>>& nodeB) {
  return !(nodeA == nodeB);
}

template <typename TC, typename TType, bool Immutable>
bool operator<(
    const ThriftPrimitiveNode<TC, TType, Immutable>& nodeA,
    const ThriftPrimitiveNode<TC, TType, Immutable>& nodeB) {
  return nodeA.cref() < nodeB.cref();
}

template <typename TC, typename TType, bool Immutable>
bool operator<(
    const std::optional<ThriftPrimitiveNode<TC, TType, Immutable>>& nodeA,
    const std::optional<ThriftPrimitiveNode<TC, TType, Immutable>>& nodeB) {
  return *nodeA < *nodeB;
}

} // namespace facebook::fboss::thrift_cow

namespace std {

template <typename TC, typename TType, bool Immutable>
struct hash<
    facebook::fboss::thrift_cow::ThriftPrimitiveNode<TC, TType, Immutable>> {
  using Key =
      facebook::fboss::thrift_cow::ThriftPrimitiveNode<TC, TType, Immutable>;

  std::size_t operator()(const Key& key) {
    return key.hash();
  }
};

} // namespace std
