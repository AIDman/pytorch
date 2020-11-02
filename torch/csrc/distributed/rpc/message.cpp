#include <torch/csrc/distributed/rpc/message.h>

namespace torch {
namespace distributed {
namespace rpc {

namespace {
enum MessageIValueIdx {
  PAYLOAD = 0,
  TENSORS,
  TYPE,
  ID,
  SIZE // must be last in list
};
}

Message::Message() = default;

Message::Message(
    std::vector<char>&& payload,
    std::vector<torch::Tensor>&& tensors,
    MessageType type)
    : payload_(std::move(payload)), tensors_(std::move(tensors)), type_(type) {}

Message::Message(
    std::vector<char>&& payload,
    std::vector<torch::Tensor>&& tensors,
    MessageType type,
    int64_t id)
    : payload_(std::move(payload)),
      tensors_(std::move(tensors)),
      type_(type),
      id_(id) {}

Message::Message(const Message& other) = default;

Message::Message(Message&& other) noexcept = default;

Message& Message::operator=(Message const& rhs) & {
  auto payload = rhs.payload_;
  auto tensors = rhs.tensors_;
  Message(std::move(payload), std::move(tensors), rhs.type_, rhs.id_)
      .swap(*this);
  return *this;
}

Message& Message::operator=(Message&& rhs) & {
  Message(std::move(rhs.payload_), std::move(rhs.tensors_), rhs.type_, rhs.id_)
      .swap(*this);
  return *this;
}

void Message::swap(Message& rhs) noexcept {
  std::swap(payload_, rhs.payload_);
  std::swap(tensors_, rhs.tensors_);
  std::swap(type_, rhs.type_);
  std::swap(id_, rhs.id_);
}

std::vector<char>&& Message::movePayload() && {
  return std::move(payload_);
}

std::vector<char>& Message::payload() {
  return payload_;
}

const std::vector<char>& Message::payload() const {
  return payload_;
}

std::vector<torch::Tensor>&& Message::moveTensors() && {
  return std::move(tensors_);
}

std::vector<torch::Tensor>& Message::tensors() {
  return tensors_;
}

const std::vector<torch::Tensor>& Message::tensors() const {
  return tensors_;
}

MessageType Message::type() const {
  return type_;
}

bool Message::isRequest() const {
  return MessageType::SCRIPT_CALL == type_ || // dist.rpc on builtin ops
      MessageType::PYTHON_CALL == type_ || // dist.rpc on Python UDFs
      MessageType::SCRIPT_REMOTE_CALL == type_ || // dist.remote on builtin ops
      MessageType::PYTHON_REMOTE_CALL == type_ || // dist.remote on Python UDFs
      // RRef related internal messages
      MessageType::SCRIPT_RREF_FETCH_CALL == type_ ||
      MessageType::PYTHON_RREF_FETCH_CALL == type_ ||
      MessageType::RREF_USER_DELETE == type_ ||
      MessageType::RREF_CHILD_ACCEPT == type_ ||
      MessageType::RREF_FORK_REQUEST == type_ ||
      // Autograd message
      MessageType::BACKWARD_AUTOGRAD_REQ == type_ ||
      MessageType::FORWARD_AUTOGRAD_REQ == type_ ||
      // Cleanup Autograd context request
      MessageType::CLEANUP_AUTOGRAD_CONTEXT_REQ == type_ ||
      // Run with profiling request
      MessageType::RUN_WITH_PROFILING_REQ == type_;
}

bool Message::isResponse() const {
  return MessageType::SCRIPT_RET == type_ || // ret of dist.rpc on builtin ops
      MessageType::PYTHON_RET == type_ || // ret of dist.rpc on Python UDFs
      MessageType::REMOTE_RET == type_ || // ret of dist.remote
      MessageType::SCRIPT_RREF_FETCH_RET == type_ || // ret on RRef::toHere()
      MessageType::PYTHON_RREF_FETCH_RET == type_ || // ret on RRef::toHere()
      MessageType::EXCEPTION == type_ || // propagate back exceptions
      MessageType::RREF_ACK == type_ || // ret of other types
      // Autograd response
      MessageType::BACKWARD_AUTOGRAD_RESP == type_ ||
      MessageType::FORWARD_AUTOGRAD_RESP == type_ ||
      // Cleanup autograd context response
      MessageType::CLEANUP_AUTOGRAD_CONTEXT_RESP == type_ ||
      // Run with profiling response
      MessageType::RUN_WITH_PROFILING_RESP == type_;
}

int64_t Message::id() const {
  return id_;
}

void Message::setId(int64_t id) {
  id_ = id;
}

at::IValue Message::toIValueTuple() const {
  // Message has a std::vector<char> payload_ that is represented via a
  // string, list of tensors, integer message type, and int64 id.
  std::string payload(payload_.begin(), payload_.end());
  c10::impl::GenericList messageTensors(at::TensorType::get());
  messageTensors.reserve(tensors_.size());
  for (const auto& tensor : tensors_) {
    messageTensors.emplace_back(tensor);
  }
  return at::IValue(
      c10::ivalue::Tuple::create(payload, messageTensors, type_, id_));
}

/* static */ Message Message::fromIValueTuple(at::IValue messageTuple) {
  TORCH_INTERNAL_ASSERT(
      messageTuple.isTuple(), "Expected messageTuple to be of type tuple.");
  std::vector<at::IValue> values = messageTuple.toTuple()->elements();
  TORCH_INTERNAL_ASSERT(
      values.size() == MessageIValueIdx::SIZE,
      c10::str("Expected ", MessageIValueIdx::SIZE, " elements in tuple."));
  auto payloadIValue = values[MessageIValueIdx::PAYLOAD];
  TORCH_INTERNAL_ASSERT(
      payloadIValue.isString(), "Expected payload to be string");
  auto payloadString = payloadIValue.toStringRef();
  std::vector<char> payload(payloadString.begin(), payloadString.end());
  auto tensorsIValue = values[MessageIValueIdx::TENSORS];
  TORCH_INTERNAL_ASSERT(
      tensorsIValue.isList(), "Expected tensorsIValue to be list");
  std::vector<torch::Tensor> tensors = tensorsIValue.toTensorVector();

  auto messageTypeIValue = values[MessageIValueIdx::TYPE];
  TORCH_INTERNAL_ASSERT(
      messageTypeIValue.isInt(), "Expected messageTypeIValue to be int.");
  MessageType messageType = static_cast<MessageType>(messageTypeIValue.toInt());
  auto messageIdIValue = values[MessageIValueIdx::ID];
  TORCH_INTERNAL_ASSERT(
      messageIdIValue.isInt(), "Expected messageIdIValue to be int.");
  int64_t messageId = messageIdIValue.toInt();
  return Message(
      std::move(payload), std::move(tensors), messageType, messageId);
}

Message createExceptionResponse(const std::exception& e, int64_t id) {
  return createExceptionResponse(e.what(), id);
}

Message createExceptionResponse(const std::string& exceptionStr, int64_t id) {
  std::vector<char> payload(exceptionStr.begin(), exceptionStr.end());
  return Message(
      std::move(payload),
      std::vector<torch::Tensor>(),
      MessageType::EXCEPTION,
      id);
}

} // namespace rpc
} // namespace distributed
} // namespace torch
