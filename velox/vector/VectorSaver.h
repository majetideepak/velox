/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include "velox/vector/BaseVector.h"

namespace facebook::velox {

// Serialization format used by the following function is documented at
// https://facebookincubator.github.io/velox/develop/debugging/vector-saver.html

/// Serializes the type into binary format and writes it to the provided
/// output stream. Used for testing.
void saveType(const TypePtr& type, std::ostream& out);

/// Deserializes a type serialized by 'saveType' from the provided input stream.
/// Used for testing.
TypePtr restoreType(std::istream& in);

/// Serializes the vector into binary format and writes it to the provided
/// output stream. The serialization format preserves the encoding.
void saveVector(const BaseVector& vector, std::ostream& out);

/// Serializes the vector into binary format and writes it to a new file in
/// 'filePath'. The serialization preserved encoding. Exceptions will be thrown
/// if any error occurs while writing.
void saveVectorToFile(const BaseVector* vector, const char* filePath);

/// Writes 'content' to a new file in 'filePath'. Exceptions will be thrown if
/// any error occurs while writing.
void saveStringToFile(const std::string& content, const char* filePath);

/// Deserializes a vector serialized by 'save' from the provided input stream.
VectorPtr restoreVector(std::istream& in, memory::MemoryPool* pool);

/// Reads and deserializes a vector from a file stored by saveVectorToFile()
/// method call
VectorPtr restoreVectorFromFile(const char* filePath, memory::MemoryPool* pool);

/// Reads a string from a file stored by saveStringToFile() method
std::string restoreStringFromFile(const char* filePath);

/// Serializes a SelectivityVector into binary format and writes it to the
/// provided output stream.
void saveSelectivityVector(const SelectivityVector& rows, std::ostream& out);

/// Deserializes a SelectivityVector serialized by 'saveSelectivityVector' from
/// the provided input stream.
SelectivityVector restoreSelectivityVector(std::istream& in);

void saveSelectivityVectorToFile(
    const SelectivityVector& rows,
    const char* filePath);

SelectivityVector restoreSelectivityVectorFromFile(const char* filePath);

} // namespace facebook::velox
