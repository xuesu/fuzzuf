/*
 * fuzzuf
 * Copyright (C) 2022 Ricerca Security
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 */
/**
 * @file queue.cpp
 * @brief Corpus queue of Nautilus.
 * @author Ricerca Security <fuzzuf-dev@ricsec.co.jp>
 */
#include "fuzzuf/algorithms/nautilus/fuzzer/queue.hpp"
#include "fuzzuf/exceptions.hpp"
#include "fuzzuf/utils/common.hpp"


namespace fuzzuf::algorithm::nautilus::fuzzer {


void Queue::Add(Tree&& tree,
                const std::vector<uint8_t>&& all_bits,
                PUTExitReasonType exit_reason,
                Context& ctx,
                uint64_t execution_time) {
  /* Check if all bits are zero */
  bool all_zero = true;
  for (size_t i = 0; i < all_bits.size(); i++) {
    if (all_bits[i] != 0
        && _bit_to_inputs.find(i) == _bit_to_inputs.end()) {
      all_zero = false;
      break;
    }
  }
  if (all_zero) return;

  std::unordered_set<size_t> fresh_bits;
  for (size_t i = 0; i < all_bits.size(); i++) {
    if (all_bits[i]) {
      if (_bit_to_inputs.find(i) == _bit_to_inputs.end()) {
        fresh_bits.insert(i);
        _bit_to_inputs[i] = {};
      }

      _bit_to_inputs[i].push_back(_current_id);
    }
  }

  /* Stringify tree */
  std::string buffer;
  tree.UnparseTo(ctx, buffer);

  /* Create file for entry */
  std::string filepath = Util::StrPrintf(
    "%s/queue/id:%09ld,er:%d",
    _work_dir.c_str(), _current_id, exit_reason
  );
  int fd = Util::OpenFile(filepath,
                          O_WRONLY | O_CREAT | O_TRUNC,
                          S_IWUSR | S_IRUSR); // 0600
  if (fd == -1) {
    throw exceptions::unable_to_create_file(
      Util::StrPrintf("Cannot save tree: %s", filepath.c_str()),
      __FILE__, __LINE__
    );
  }
  Util::WriteFile(fd, buffer.data(), buffer.size());
  Util::CloseFile(fd);

  /* Add entry to queue */
  _inputs.emplace_back(_current_id,
                       std::move(tree),
                       std::move(fresh_bits),
                       std::move(all_bits),
                       exit_reason,
                       execution_time);

  /* Increment current_id */
  if (_current_id == std::numeric_limits<size_t>::max()) {
    _current_id = 0;
  } else {
    _current_id++;
  }
}

/**
 * @fn
 * @brief Pop an item from queue
 * @return Top item of queue
 */
QueueItem Queue::Pop() {
  DEBUG_ASSERT (!IsEmpty());

  QueueItem item = _inputs.back();
  _inputs.pop_back();

  size_t id = item.id;

  std::vector<size_t> to_remove;

  for (auto it = _bit_to_inputs.begin(); it != _bit_to_inputs.end(); ++it) {
    auto& [k, v] = *it;

    /* Retain elements in v */
    for (auto vit = v.rbegin(); vit != v.rend(); ++vit) {
      if (*vit == id) {
        v.erase(--(vit.base()));
      }
    }

    if (v.empty()) {
      to_remove.push_back(k);
    }
  }

  // TODO: O(N)? any other way to remove elements while iterating a hashmap?
  for (size_t k: to_remove) {
    _bit_to_inputs.erase(k);
  }

  return item;
}

/**
 * @fn
 * @brief Check if queue is empty
 * @return True if queue is empty, otherwise false
 */
bool Queue::IsEmpty() const {
  return _inputs.size() == 0;
}

/**
 * @fn
 * @brief Mark item as finished
 * @param (item) Item
 */
void Queue::Finished(QueueItem&& item) {
  bool all_zero = true;
  for (size_t i = 0; i < item.all_bits.size(); i++) {
    if (item.all_bits[i] != 0
        && _bit_to_inputs.find(i) == _bit_to_inputs.end()) {
      all_zero = false;
      break;
    }
  }

  if (all_zero) {
    Util::DeleteFileOrDirectory(
      Util::StrPrintf("%s/outputs/queue/id:%09ld,er:%d",
                      _work_dir.c_str(), item.id, item.exit_reason)
    );
    return;
  }

  std::unordered_set<size_t> fresh_bits;
  for (size_t i = 0; i < item.all_bits.size(); i++) {
    if (item.all_bits[i]) {
      if (_bit_to_inputs.find(i) == _bit_to_inputs.end()) {
        fresh_bits.insert(i);
        _bit_to_inputs[i] = {};
      }

      _bit_to_inputs[i].push_back(item.id);
    }
  }

  _processed.emplace_back(std::move(item));
}

/**
 * @fn
 * @brief Put processed items into inputs
 */
void Queue::NewRound() {
  _inputs.insert(_inputs.end(),
                 _processed.begin(), _processed.end());
}

} // namespace fuzzuf::algorithm::nautilus::fuzzer
