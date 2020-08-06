#include "generic/stage2/json_iterator.h"
#include "generic/stage2/tape_writer.h"
#include "generic/stage2/atomparsing.h"

namespace {
namespace SIMDJSON_IMPLEMENTATION {
namespace stage2 {

struct tape_builder {
  template<bool STREAMING>
  WARN_UNUSED static really_inline error_code parse_document(
      dom_parser_implementation &dom_parser,
      dom::document &doc) noexcept {
    dom_parser.doc = &doc;
    json_iterator iter(dom_parser, STREAMING ? dom_parser.next_structural_index : 0);
    tape_builder builder(doc);
    return iter.walk_document<STREAMING>(builder);
  }

  WARN_UNUSED really_inline error_code root_primitive(json_iterator &iter, const uint8_t *value) {
    switch (*value) {
      case '"': return parse_string(iter, value);
      case 't': return parse_root_true_atom(iter, value);
      case 'f': return parse_root_false_atom(iter, value);
      case 'n': return parse_root_null_atom(iter, value);
      case '-':
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
        return parse_root_number(iter, value);
      default:
        iter.log_error("Document starts with a non-value character");
        return TAPE_ERROR;
    }
  }
  WARN_UNUSED really_inline error_code primitive(json_iterator &iter, const uint8_t *value) {
    switch (*value) {
      case '"': return parse_string(iter, value);
      case 't': return parse_true_atom(iter, value);
      case 'f': return parse_false_atom(iter, value);
      case 'n': return parse_null_atom(iter, value);
      case '-':
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
        return parse_number(iter, value);
      default:
        iter.log_error("Non-value found when value was expected!");
        return TAPE_ERROR;
    }
  }
  WARN_UNUSED really_inline error_code empty_object(json_iterator &iter) {
    iter.log_value("empty object");
    return empty_container(iter, internal::tape_type::START_OBJECT, internal::tape_type::END_OBJECT);
  }
  WARN_UNUSED really_inline error_code empty_array(json_iterator &iter) {
    iter.log_value("empty array");
    return empty_container(iter, internal::tape_type::START_ARRAY, internal::tape_type::END_ARRAY);
  }

  WARN_UNUSED really_inline error_code start_document(json_iterator &iter) {
    iter.log_start_value("document");
    start_container(iter);
    iter.dom_parser.is_array[depth] = false;
    return SUCCESS;
  }
  WARN_UNUSED really_inline error_code start_object(json_iterator &iter) {
    iter.log_start_value("object");
    depth++;
    if (depth >= iter.dom_parser.max_depth()) { iter.log_error("Exceeded max depth!"); return DEPTH_ERROR; }
    start_container(iter);
    iter.dom_parser.is_array[depth] = false;
    return SUCCESS;
  }
  WARN_UNUSED really_inline error_code start_array(json_iterator &iter) {
    iter.log_start_value("array");
    depth++;
    if (depth >= iter.dom_parser.max_depth()) { iter.log_error("Exceeded max depth!"); return DEPTH_ERROR; }
    start_container(iter);
    iter.dom_parser.is_array[depth] = true;
    return SUCCESS;
  }

  WARN_UNUSED really_inline error_code end_object(json_iterator &iter) {
    iter.log_end_value("object");
    return end_container(iter, internal::tape_type::START_OBJECT, internal::tape_type::END_OBJECT);
  }
  WARN_UNUSED really_inline error_code end_array(json_iterator &iter) {
    iter.log_end_value("array");
    return end_container(iter, internal::tape_type::START_ARRAY, internal::tape_type::END_ARRAY);
  }
  WARN_UNUSED really_inline error_code end_document(json_iterator &iter) {
    iter.log_end_value("document");
    constexpr uint32_t start_tape_index = 0;
    tape.append(start_tape_index, internal::tape_type::ROOT);
    tape_writer::write(iter.dom_parser.doc->tape[start_tape_index], next_tape_index(iter), internal::tape_type::ROOT);
    iter.dom_parser.next_structural_index = uint32_t(iter.next_structural - &iter.dom_parser.structural_indexes[0]);
    if (depth != 0) {
      iter.log_error("Unclosed objects or arrays!");
      return TAPE_ERROR;
    }
    return SUCCESS;
  }
  WARN_UNUSED really_inline error_code key(json_iterator &iter, const uint8_t *value) {
    return parse_string(iter, value, true);
  }

  WARN_UNUSED really_inline error_code next_array_element(json_iterator &iter) {
    return increment_count(iter);
  }
  WARN_UNUSED really_inline error_code next_field(json_iterator &iter) {
    return increment_count(iter);
  }

  // Called after end_object/end_array. Not called after empty_object/empty_array,
  // as the parent is already known in those cases.
  //
  // The object returned from end_container() should support the in_container(),
  // in_array() and in_object() methods, allowing the iterator to branch to the
  // correct place.
  really_inline tape_builder &end_container(json_iterator &) {
    depth--;
    return *this;
  }
  really_inline bool in_container(json_iterator &) {
    return depth != 0;
  }
  really_inline bool in_array(json_iterator &iter) {
    return iter.dom_parser.is_array[depth];
  }
  really_inline bool in_object(json_iterator &iter) {
    return !iter.dom_parser.is_array[depth];
  }

private:
  /** Next location to write to tape */
  tape_writer tape;
  /** Next write location in the string buf for stage 2 parsing */
  uint8_t *current_string_buf_loc;
  /** Current depth (nested objects and arrays) */
  uint32_t depth{0};

  really_inline tape_builder(dom::document &doc) noexcept : tape{doc.tape.get()}, current_string_buf_loc{doc.string_buf.get()} {}

  // increment_count increments the count of keys in an object or values in an array.
  WARN_UNUSED really_inline error_code increment_count(json_iterator &iter) {
    iter.dom_parser.open_containers[depth].count++; // we have a key value pair in the object at parser.dom_parser.depth - 1
    return SUCCESS;
  }

  WARN_UNUSED really_inline error_code parse_string(json_iterator &iter, const uint8_t *value, bool key = false) {
    iter.log_value(key ? "key" : "string");
    uint8_t *dst = on_start_string(iter);
    dst = stringparsing::parse_string(value, dst);
    if (dst == nullptr) {
      iter.log_error("Invalid escape in string");
      return STRING_ERROR;
    }
    on_end_string(dst);
    return SUCCESS;
  }

  WARN_UNUSED really_inline error_code parse_number(json_iterator &iter, const uint8_t *value) {
    iter.log_value("number");
    if (!numberparsing::parse_number(value, tape)) { iter.log_error("Invalid number"); return NUMBER_ERROR; }
    return SUCCESS;
  }

  WARN_UNUSED really_inline error_code parse_root_number(json_iterator &iter, const uint8_t *value) {
    //
    // We need to make a copy to make sure that the string is space terminated.
    // This is not about padding the input, which should already padded up
    // to len + SIMDJSON_PADDING. However, we have no control at this stage
    // on how the padding was done. What if the input string was padded with nulls?
    // It is quite common for an input string to have an extra null character (C string).
    // We do not want to allow 9\0 (where \0 is the null character) inside a JSON
    // document, but the string "9\0" by itself is fine. So we make a copy and
    // pad the input with spaces when we know that there is just one input element.
    // This copy is relatively expensive, but it will almost never be called in
    // practice unless you are in the strange scenario where you have many JSON
    // documents made of single atoms.
    //
    uint8_t *copy = static_cast<uint8_t *>(malloc(iter.remaining_len() + SIMDJSON_PADDING));
    if (copy == nullptr) {
      return MEMALLOC;
    }
    memcpy(copy, value, iter.remaining_len());
    memset(copy + iter.remaining_len(), ' ', SIMDJSON_PADDING);
    error_code error = parse_number(iter, copy);
    free(copy);
    return error;
  }

  WARN_UNUSED really_inline error_code parse_true_atom(json_iterator &iter, const uint8_t *value) {
    iter.log_value("true");
    if (!atomparsing::is_valid_true_atom(value)) { return T_ATOM_ERROR; }
    tape.append(0, internal::tape_type::TRUE_VALUE);
    return SUCCESS;
  }

  WARN_UNUSED really_inline error_code parse_root_true_atom(json_iterator &iter, const uint8_t *value) {
    iter.log_value("true");
    if (!atomparsing::is_valid_true_atom(value, iter.remaining_len())) { return T_ATOM_ERROR; }
    tape.append(0, internal::tape_type::TRUE_VALUE);
    return SUCCESS;
  }

  WARN_UNUSED really_inline error_code parse_false_atom(json_iterator &iter, const uint8_t *value) {
    iter.log_value("false");
    if (!atomparsing::is_valid_false_atom(value)) { return F_ATOM_ERROR; }
    tape.append(0, internal::tape_type::FALSE_VALUE);
    return SUCCESS;
  }

  WARN_UNUSED really_inline error_code parse_root_false_atom(json_iterator &iter, const uint8_t *value) {
    iter.log_value("false");
    if (!atomparsing::is_valid_false_atom(value, iter.remaining_len())) { return F_ATOM_ERROR; }
    tape.append(0, internal::tape_type::FALSE_VALUE);
    return SUCCESS;
  }

  WARN_UNUSED really_inline error_code parse_null_atom(json_iterator &iter, const uint8_t *value) {
    iter.log_value("null");
    if (!atomparsing::is_valid_null_atom(value)) { return N_ATOM_ERROR; }
    tape.append(0, internal::tape_type::NULL_VALUE);
    return SUCCESS;
  }

  WARN_UNUSED really_inline error_code parse_root_null_atom(json_iterator &iter, const uint8_t *value) {
    iter.log_value("null");
    if (!atomparsing::is_valid_null_atom(value, iter.remaining_len())) { return N_ATOM_ERROR; }
    tape.append(0, internal::tape_type::NULL_VALUE);
    return SUCCESS;
  }

// private:

  really_inline uint32_t next_tape_index(json_iterator &iter) {
    return uint32_t(tape.next_tape_loc - iter.dom_parser.doc->tape.get());
  }

  WARN_UNUSED really_inline error_code empty_container(json_iterator &iter, internal::tape_type start, internal::tape_type end) {
    auto start_index = next_tape_index(iter);
    tape.append(start_index+2, start);
    tape.append(start_index, end);
    return SUCCESS;
  }

  really_inline void start_container(json_iterator &iter) {
    iter.dom_parser.open_containers[depth].tape_index = next_tape_index(iter);
    iter.dom_parser.open_containers[depth].count = 1;
    tape.skip(); // We don't actually *write* the start element until the end.
  }

  WARN_UNUSED really_inline error_code end_container(json_iterator &iter, internal::tape_type start, internal::tape_type end) noexcept {
    // Write the ending tape element, pointing at the start location
    const uint32_t start_tape_index = iter.dom_parser.open_containers[depth].tape_index;
    tape.append(start_tape_index, end);
    // Write the start tape element, pointing at the end location (and including count)
    // count can overflow if it exceeds 24 bits... so we saturate
    // the convention being that a cnt of 0xffffff or more is undetermined in value (>=  0xffffff).
    const uint32_t count = iter.dom_parser.open_containers[depth].count;
    const uint32_t cntsat = count > 0xFFFFFF ? 0xFFFFFF : count;
    tape_writer::write(iter.dom_parser.doc->tape[start_tape_index], next_tape_index(iter) | (uint64_t(cntsat) << 32), start);
    return SUCCESS;
  }

  really_inline uint8_t *on_start_string(json_iterator &iter) noexcept {
    // we advance the point, accounting for the fact that we have a NULL termination
    tape.append(current_string_buf_loc - iter.dom_parser.doc->string_buf.get(), internal::tape_type::STRING);
    return current_string_buf_loc + sizeof(uint32_t);
  }

  really_inline void on_end_string(uint8_t *dst) noexcept {
    uint32_t str_length = uint32_t(dst - (current_string_buf_loc + sizeof(uint32_t)));
    // TODO check for overflow in case someone has a crazy string (>=4GB?)
    // But only add the overflow check when the document itself exceeds 4GB
    // Currently unneeded because we refuse to parse docs larger or equal to 4GB.
    memcpy(current_string_buf_loc, &str_length, sizeof(uint32_t));
    // NULL termination is still handy if you expect all your strings to
    // be NULL terminated? It comes at a small cost
    *dst = 0;
    current_string_buf_loc = dst + 1;
  }
}; // class tape_builder

} // namespace stage2
} // namespace SIMDJSON_IMPLEMENTATION
} // unnamed namespace
