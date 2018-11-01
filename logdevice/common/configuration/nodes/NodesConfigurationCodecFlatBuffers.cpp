/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include "NodesConfigurationCodecFlatBuffers.h"

#include "logdevice/common/debug.h"
#include "logdevice/common/membership/MembershipCodecFlatBuffers.h"
#include "logdevice/include/Err.h"

namespace facebook { namespace logdevice { namespace configuration {
namespace nodes {

constexpr NodesConfigurationCodecFlatBuffers::ProtocolVersion
    NodesConfigurationCodecFlatBuffers::CURRENT_PROTO_VERSION;

namespace {

template <typename T>
flatbuffers::Offset<flatbuffers::String>
serializeOptionalStrField(flatbuffers::FlatBufferBuilder& b,
                          const T& op_field) {
  return op_field.hasValue() ? b.CreateString(op_field.value().toString()) : 0;
}

} // namespace

//////////////////////// NodeServiceDiscovery //////////////////////////////

/*static*/
flatbuffers::Offset<flat_buffer_codec::NodeServiceDiscovery>
NodesConfigurationCodecFlatBuffers::serialize(
    flatbuffers::FlatBufferBuilder& b,
    const NodeServiceDiscovery& discovery) {
  return flat_buffer_codec::CreateNodeServiceDiscovery(
      b,
      b.CreateString(discovery.address.toString()),
      b.CreateString(discovery.gossip_address.toString()),
      serializeOptionalStrField(b, discovery.ssl_address),
      serializeOptionalStrField(b, discovery.admin_address),
      serializeOptionalStrField(b, discovery.location),
      discovery.roles.to_ullong(),
      b.CreateString(discovery.hostname));
}

/*static*/
int NodesConfigurationCodecFlatBuffers::deserialize(
    const flat_buffer_codec::NodeServiceDiscovery* obj,
    NodeServiceDiscovery* out) {
  ld_check(obj != nullptr);
  NodeServiceDiscovery result;

#define PARSE_SOCK_FIELD(_name, _optional)                   \
  do {                                                       \
    if (!obj->_name()) {                                     \
      if (!_optional) {                                      \
        ld_error("Missing required field %s.", #_name);      \
        return -1;                                           \
      }                                                      \
    } else {                                                 \
      auto sock = Sockaddr::fromString(obj->_name()->str()); \
      if (!sock.hasValue()) {                                \
        ld_error("malformed socket addr field %s.", #_name); \
        return -1;                                           \
      }                                                      \
      result._name = sock.value();                           \
    }                                                        \
  } while (0)

  PARSE_SOCK_FIELD(address, false);
  PARSE_SOCK_FIELD(gossip_address, false);
  PARSE_SOCK_FIELD(ssl_address, true);
  PARSE_SOCK_FIELD(admin_address, true);

#undef PARSE_SOCK_FIELD

  if (obj->location()) {
    NodeLocation location;
    int rv = location.fromDomainString(obj->location()->str());
    if (rv != 0) {
      ld_error("Invalid \"location\" string %s", obj->location()->c_str());
      return -1;
    }
    result.location = location;
  }

  result.roles = NodeServiceDiscovery::RoleSet(obj->roles());

  if (obj->hostname()) {
    result.hostname = obj->hostname()->str();
  }

  if (out != nullptr) {
    *out = result;
  }
  return 0;
}

//////////////////////// SequencerNodeAttribute //////////////////////////////

/*static*/
flatbuffers::Offset<flat_buffer_codec::SequencerNodeAttribute>
NodesConfigurationCodecFlatBuffers::serialize(
    flatbuffers::FlatBufferBuilder& b,
    const SequencerNodeAttribute& /*unused*/) {
  return flat_buffer_codec::CreateSequencerNodeAttribute(b);
}

/*static*/
int NodesConfigurationCodecFlatBuffers::deserialize(
    const flat_buffer_codec::SequencerNodeAttribute* obj,
    SequencerNodeAttribute* out) {
  if (out != nullptr) {
    *out = SequencerNodeAttribute();
  }
  return 0;
}

//////////////////////// StorageNodeAttribute //////////////////////////////

/*static*/
flatbuffers::Offset<flat_buffer_codec::StorageNodeAttribute>
NodesConfigurationCodecFlatBuffers::serialize(
    flatbuffers::FlatBufferBuilder& b,
    const StorageNodeAttribute& storage_attr) {
  return flat_buffer_codec::CreateStorageNodeAttribute(
      b,
      storage_attr.capacity,
      storage_attr.num_shards,
      storage_attr.generation,
      storage_attr.exclude_from_nodesets);
}

/*static*/
int NodesConfigurationCodecFlatBuffers::deserialize(
    const flat_buffer_codec::StorageNodeAttribute* obj,
    StorageNodeAttribute* out) {
  ld_check(obj != nullptr);
  StorageNodeAttribute result{obj->capacity(),
                              obj->num_shards(),
                              obj->generation(),
                              obj->exclude_from_nodesets()};

  if (out != nullptr) {
    *out = result;
  }
  return 0;
}

//////////////////////// NodeAttributesConfig //////////////////////////////

#define GEN_SERIALIZATION_NODE_ATTRS_CONFIG(_Config, _Attribute)            \
  /*static*/                                                                \
  flatbuffers::Offset<flat_buffer_codec::_Config>                           \
  NodesConfigurationCodecFlatBuffers::serialize(                            \
      flatbuffers::FlatBufferBuilder& _b, const _Config& _config) {         \
    std::vector<flatbuffers::Offset<flat_buffer_codec::_Config##MapItem>>   \
        node_states;                                                        \
    for (const auto& node_kv : (_config).node_states_) {                    \
      node_states.push_back(flat_buffer_codec::Create##_Config##MapItem(    \
          (_b), node_kv.first, serialize((_b), node_kv.second)));           \
    }                                                                       \
    return flat_buffer_codec::Create##_Config(                              \
        (_b), (_b).CreateVector(node_states));                              \
  }                                                                         \
                                                                            \
  /*static*/                                                                \
  std::shared_ptr<_Config> NodesConfigurationCodecFlatBuffers::deserialize( \
      const flat_buffer_codec::_Config* _fb_config) {                       \
    ld_check(_fb_config != nullptr);                                        \
    auto result = std::make_shared<_Config>();                              \
    auto node_states = _fb_config->node_states();                           \
    if (node_states) {                                                      \
      for (size_t i = 0; i < node_states->Length(); ++i) {                  \
        auto node_state = node_states->Get(i);                              \
        node_index_t node = node_state->node_idx();                         \
        auto node_attribute = node_state->node_attribute();                 \
        if (node_attribute == nullptr) {                                    \
          ld_error("Node %hu does not have an attribute.", node);           \
          err = E::INVALID_CONFIG;                                          \
          return nullptr;                                                   \
        }                                                                   \
        if (result->hasNode(node)) {                                        \
          ld_error("Duplicate Node %hu in the config.", node);              \
          err = E::INVALID_CONFIG;                                          \
          return nullptr;                                                   \
        }                                                                   \
        _Attribute attr;                                                    \
        int rv = deserialize(node_attribute, &attr);                        \
        if (rv != 0) {                                                      \
          err = E::INVALID_CONFIG;                                          \
          return nullptr;                                                   \
        }                                                                   \
        result->setNodeAttributes(node, attr);                              \
      }                                                                     \
    }                                                                       \
    /* note: we don't do validation here since it will be done */           \
    /* at NodesConfiguration deserialization */                             \
    return result;                                                          \
  }

GEN_SERIALIZATION_NODE_ATTRS_CONFIG(ServiceDiscoveryConfig,
                                    NodeServiceDiscovery)
GEN_SERIALIZATION_NODE_ATTRS_CONFIG(SequencerAttributeConfig,
                                    SequencerNodeAttribute)
GEN_SERIALIZATION_NODE_ATTRS_CONFIG(StorageAttributeConfig,
                                    StorageNodeAttribute)

#undef GEN_SERIALIZATION_NODE_ATTRS_CONFIG

//////////////////////// PerRoleConfig //////////////////////////////

#define GEN_SERIALIZATION_PER_ROLE_CONFIG(_Config, _AttrConfig, _Membership) \
  /*static*/                                                                 \
  flatbuffers::Offset<flat_buffer_codec::_Config>                            \
  NodesConfigurationCodecFlatBuffers::serialize(                             \
      flatbuffers::FlatBufferBuilder& _b, const _Config& _config) {          \
    /* must serialize a valid config */                                      \
    ld_check(_config.membership_ != nullptr);                                \
    ld_check(_config.attributes_ != nullptr);                                \
    return flat_buffer_codec::Create##_Config(                               \
        _b,                                                                  \
        serialize(_b, *_config.attributes_),                                 \
        membership::MembershipCodecFlatBuffers::serialize(                   \
            _b, *_config.membership_));                                      \
  }                                                                          \
                                                                             \
  /*static*/                                                                 \
  std::shared_ptr<_Config> NodesConfigurationCodecFlatBuffers::deserialize(  \
      const flat_buffer_codec::_Config* _fb_config) {                        \
    if (_fb_config->attr_conf() == nullptr) {                                \
      ld_error("Attribute config missing for %s.", #_Config);                \
      err = E::INVALID_CONFIG;                                               \
      return nullptr;                                                        \
    }                                                                        \
    if (_fb_config->membership() == nullptr) {                               \
      ld_error("Membership missing for %s.", #_Config);                      \
      err = E::INVALID_CONFIG;                                               \
      return nullptr;                                                        \
    }                                                                        \
    auto attr_config = deserialize(_fb_config->attr_conf());                 \
    if (attr_config == nullptr) {                                            \
      err = E::INVALID_CONFIG;                                               \
      return nullptr;                                                        \
    }                                                                        \
    auto membership = membership::MembershipCodecFlatBuffers::deserialize(   \
        _fb_config->membership());                                           \
    if (membership == nullptr) {                                             \
      err = E::INVALID_CONFIG;                                               \
      return nullptr;                                                        \
    }                                                                        \
    return std::make_shared<_Config>(                                        \
        std::move(membership), std::move(attr_config));                      \
  }

GEN_SERIALIZATION_PER_ROLE_CONFIG(SequencerConfig,
                                  SequencerAttributeConfig,
                                  membership::SequencerMembership)
GEN_SERIALIZATION_PER_ROLE_CONFIG(StorageConfig,
                                  StorageAttributeConfig,
                                  membership::StorageMembership)

#undef GEN_SERIALIZATION_PER_ROLE_CONFIG

//////////////////////// MetaDataLogsReplication //////////////////////////////

/* static */
flatbuffers::Offset<flat_buffer_codec::MetaDataLogsReplication>
NodesConfigurationCodecFlatBuffers::serialize(
    flatbuffers::FlatBufferBuilder& b,
    const MetaDataLogsReplication& config) {
  // must serialize a valid config
  ld_check(config.validate());
  std::vector<flat_buffer_codec::ScopeReplication> scopes;
  for (const auto& reps : config.replication_.getDistinctReplicationFactors()) {
    scopes.push_back(flat_buffer_codec::ScopeReplication(
        static_cast<uint8_t>(reps.first), reps.second));
  }

  return flat_buffer_codec::CreateMetaDataLogsReplication(
      b,
      config.version_.val(),
      flat_buffer_codec::CreateReplicationProperty(
          b, b.CreateVectorOfStructs(scopes)));
}

/* static */
std::shared_ptr<MetaDataLogsReplication>
NodesConfigurationCodecFlatBuffers::deserialize(
    const flat_buffer_codec::MetaDataLogsReplication* flat_buffer_config) {
  ld_check(flat_buffer_config != nullptr);

  auto result = std::make_shared<MetaDataLogsReplication>();
  if (flat_buffer_config->replication() == nullptr) {
    ld_error("No replication property provided in MetaDataLogsReplication");
    err = E::INVALID_CONFIG;
    return nullptr;
  }
  std::vector<ReplicationProperty::ScopeReplication> scopes;
  auto rep_scopes = flat_buffer_config->replication()->scopes();
  if (rep_scopes) {
    for (size_t i = 0; i < rep_scopes->Length(); ++i) {
      auto scope = rep_scopes->Get(i);
      scopes.emplace_back(static_cast<NodeLocationScope>(scope->scope()),
                          static_cast<int>(scope->replication_factor()));
    }
  }

  result->version_ =
      membership::MembershipVersion::Type(flat_buffer_config->version());

  // allow empty scopes here (which is prohibited in
  // ReplicationProperty::assign())
  if (!scopes.empty()) {
    int rv = result->replication_.assign(std::move(scopes));
    if (rv != 0) {
      ld_error("Invalid replication property for metadata logs replication.");
      return nullptr;
    }
  }

  return result;
}

//////////////////////// NodesConfiguration //////////////////////////////

/* static */
flatbuffers::Offset<flat_buffer_codec::NodesConfiguration>
NodesConfigurationCodecFlatBuffers::serialize(
    flatbuffers::FlatBufferBuilder& b,
    const NodesConfiguration& config) {
  // config must have valid components
  ld_check(config.service_discovery_ != nullptr);
  ld_check(config.sequencer_config_ != nullptr);
  ld_check(config.storage_config_ != nullptr);
  ld_check(config.metadata_logs_rep_ != nullptr);

  return flat_buffer_codec::CreateNodesConfiguration(
      b,
      CURRENT_PROTO_VERSION,
      config.getVersion().val(),
      serialize(b, *config.service_discovery_),
      serialize(b, *config.sequencer_config_),
      serialize(b, *config.storage_config_),
      serialize(b, *config.metadata_logs_rep_),
      config.last_change_timestamp_,
      config.last_maintenance_.val(),
      b.CreateString(config.last_change_context_));
}

/* static */
std::shared_ptr<NodesConfiguration>
NodesConfigurationCodecFlatBuffers::deserialize(
    const flat_buffer_codec::NodesConfiguration* fb_config) {
  ld_check(fb_config != nullptr);
  NodesConfigurationCodecFlatBuffers::ProtocolVersion pv =
      fb_config->proto_version();
  if (pv > CURRENT_PROTO_VERSION) {
    RATELIMIT_ERROR(
        std::chrono::seconds(10),
        5,
        "Received codec protocol version %u is larger than current "
        "codec protocol version %u. There might be incompatible data, "
        "aborting deserialization",
        pv,
        CURRENT_PROTO_VERSION);
    err = E::NOTSUPPORTED;
    return nullptr;
  }

  auto result = std::make_shared<NodesConfiguration>();

#define PARSE_SUB_CONF(_name)                             \
  do {                                                    \
    if (fb_config->_name() == nullptr) {                  \
      ld_error("subconfig %s is missing.", #_name);       \
      err = E::INVALID_CONFIG;                            \
      return nullptr;                                     \
    }                                                     \
    result->_name##_ = deserialize(fb_config->_name());   \
    if (result->_name##_ == nullptr) {                    \
      ld_error("failure to parse subconfig %s.", #_name); \
      err = E::INVALID_CONFIG;                            \
      return nullptr;                                     \
    }                                                     \
  } while (0)

  PARSE_SUB_CONF(service_discovery);
  PARSE_SUB_CONF(sequencer_config);
  PARSE_SUB_CONF(storage_config);
  PARSE_SUB_CONF(metadata_logs_rep);
#undef PARSE_SUB_CONF

  result->version_ = membership::MembershipVersion::Type(fb_config->version());
  result->last_change_timestamp_ = fb_config->last_timestamp();
  result->last_maintenance_ =
      membership::MaintenanceID::Type(fb_config->last_maintenance());
  if (fb_config->last_context()) {
    result->last_change_context_ = fb_config->last_context()->str();
  }

  // recompute all config metadata
  result->recomputeConfigMetadata();

  // perform the final validation
  if (!result->validate()) {
    ld_error("Invalid NodesConfiguration after deserialization.");
    err = E::INVALID_CONFIG;
    return nullptr;
  }

  return result;
}

}}}} // namespace facebook::logdevice::configuration::nodes
