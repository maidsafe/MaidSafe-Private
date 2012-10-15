/***************************************************************************************************
 *  Copyright 2012 maidsafe.net limited                                                            *
 *                                                                                                 *
 *  The following source code is property of MaidSafe.net limited and is not meant for external    *
 *  use. The use of this code is governed by the licence file licence.txt found in the root of     *
 *  this directory and also on www.maidsafe.net.                                                   *
 *                                                                                                 *
 *  You are not free to copy, amend or otherwise use this source code without the explicit written *
 *  permission of the board of directors of MaidSafe.net.                                          *
 **************************************************************************************************/

#ifndef MAIDSAFE_PRIVATE_CHUNK_STORE_TESTS_CHUNK_STORE_API_TEST_H_
#define MAIDSAFE_PRIVATE_CHUNK_STORE_TESTS_CHUNK_STORE_API_TEST_H_

#include <memory>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include "boost/filesystem.hpp"
#include "boost/filesystem/fstream.hpp"
#include "boost/thread.hpp"

#include "maidsafe/common/asio_service.h"
#include "maidsafe/common/crypto.h"
#include "maidsafe/common/test.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/private/chunk_store/chunk_store.h"

namespace fs = boost::filesystem;

namespace maidsafe {

namespace priv {

namespace chunk_store {

namespace test {

template <typename T>
class ChunkStoreTest: public testing::Test {
 public:
  ChunkStoreTest()
      : test_dir_(maidsafe::test::CreateTestPath("MaidSafe_TestChunkStore")),
        chunk_dir_(*test_dir_ / "chunks"),
        alt_chunk_dir_(*test_dir_ / "chunks_alt"),
        tiger_chunk_dir_(*test_dir_ / "chunks_tiger"),
        asio_service_(3),
        chunk_store_(),
        alt_chunk_store_(),
        tiger_chunk_store_() {}
  ~ChunkStoreTest() {}

 protected:
  void SetUp() {
    asio_service_.Start();
    fs::create_directories(chunk_dir_);
    fs::create_directories(alt_chunk_dir_);
    fs::create_directories(tiger_chunk_dir_);
    InitChunkStore(&chunk_store_, chunk_dir_, asio_service_.service());
    InitChunkStore(&alt_chunk_store_, alt_chunk_dir_, asio_service_.service());
    InitChunkStore(&tiger_chunk_store_, tiger_chunk_dir_, asio_service_.service());
  }

  void TearDown() {
    asio_service_.Stop();
  }

  void InitChunkStore(std::shared_ptr<ChunkStore>* chunk_store,
                      const fs::path& chunk_dir,
                      boost::asio::io_service& asio_service);

  fs::path CreateRandomFile(const fs::path& file_path, const uint64_t& file_size) {
    fs::ofstream ofs(file_path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (file_size != 0) {
      size_t string_size = (file_size > 100000) ? 100000 : static_cast<size_t>(file_size);
      uint64_t remaining_size = file_size;
      std::string rand_str = RandomString(2 * string_size);
      std::string file_content;
      uint64_t start_pos = 0;
      while (remaining_size) {
        srand(17);
        start_pos = rand() % string_size;  // NOLINT (Fraser)
        if (remaining_size < string_size) {
          string_size = static_cast<size_t>(remaining_size);
          file_content = rand_str.substr(0, string_size);
        } else {
          file_content = rand_str.substr(static_cast<size_t>(start_pos), string_size);
        }
        ofs.write(file_content.c_str(), file_content.size());
        remaining_size -= string_size;
      }
    }
    ofs.close();
    return file_path;
  }

  maidsafe::test::TestPath test_dir_;
  fs::path chunk_dir_, alt_chunk_dir_, tiger_chunk_dir_;
  AsioService asio_service_;
  std::shared_ptr<ChunkStore> chunk_store_,
                              alt_chunk_store_,
                              tiger_chunk_store_;  // mmmm, tiger chunks...
};

TYPED_TEST_CASE_P(ChunkStoreTest);

TYPED_TEST_P(ChunkStoreTest, BEH_Init) {
  EXPECT_EQ(0, this->chunk_store_->Size());
  std::string name = typeid(TypeParam).name();
  if (std::string(typeid(TypeParam).name()).find("Buffered") == std::string::npos &&
      std::string(typeid(TypeParam).name()).find("File") == std::string::npos) {
    EXPECT_EQ(0, this->chunk_store_->Capacity());
  }
  EXPECT_EQ(0, this->chunk_store_->Count());
  EXPECT_TRUE(this->chunk_store_->Empty());
  EXPECT_THROW(this->chunk_store_->Has(ChunkId()), std::exception);
}

TYPED_TEST_P(ChunkStoreTest, BEH_Get) {
  NonEmptyString content(RandomString(100));
  ChunkId name(crypto::Hash<crypto::SHA512>(content));
  fs::path path(*this->test_dir_ / "chunk.dat");
  ASSERT_FALSE(fs::exists(path));

  // non-existant chunk, should fail
  EXPECT_THROW(this->chunk_store_->Get(ChunkId()), std::exception);
  EXPECT_TRUE(this->chunk_store_->Get(name).empty());
  EXPECT_FALSE(this->chunk_store_->Get(name, path));
  EXPECT_FALSE(fs::exists(path));

  ASSERT_TRUE(this->chunk_store_->Store(name, content));

  // existing chunk
  EXPECT_EQ(content.string(), this->chunk_store_->Get(name));
  EXPECT_TRUE(this->chunk_store_->Get(name, path));
  EXPECT_TRUE(fs::exists(path));
  EXPECT_EQ(name.string(), crypto::HashFile<crypto::SHA512>(path).string());

  // existing output file, should overwrite
  this->CreateRandomFile(path, 99);
  EXPECT_NE(name.string(), crypto::HashFile<crypto::SHA512>(path).string());
  EXPECT_TRUE(this->chunk_store_->Get(name, path));
  EXPECT_EQ(name.string(), crypto::HashFile<crypto::SHA512>(path).string());

  // invalid file name
  EXPECT_FALSE(this->chunk_store_->Get(name, fs::path("")));
}

TYPED_TEST_P(ChunkStoreTest, BEH_Store) {
  NonEmptyString content(RandomString(123));
  ChunkId name_mem(crypto::Hash<crypto::SHA512>(content));
  fs::path path(*this->test_dir_ / "chunk.dat");
  this->CreateRandomFile(path, 456);
  fs::path path_empty(*this->test_dir_ / "empty.dat");
  this->CreateRandomFile(path_empty, 0);
  ChunkId name_file(crypto::HashFile<crypto::SHA512>(path));
  ASSERT_NE(name_mem, name_file);

  // invalid input
  EXPECT_THROW(this->chunk_store_->Store(name_mem, NonEmptyString()), std::exception);
  EXPECT_THROW(this->chunk_store_->Store(ChunkId(), content), std::exception);
  EXPECT_FALSE(this->chunk_store_->Store(name_file, "", false));
  EXPECT_FALSE(this->chunk_store_->Store(name_file, *this->test_dir_ / "fail", false));
  EXPECT_THROW(this->chunk_store_->Store(ChunkId(), path, false), std::exception);
  EXPECT_FALSE(this->chunk_store_->Store(name_file, path_empty, false));
  EXPECT_TRUE(this->chunk_store_->Empty());
  EXPECT_EQ(0, this->chunk_store_->Count());
  EXPECT_EQ(0, this->chunk_store_->Size());
  EXPECT_FALSE(this->chunk_store_->Has(name_mem));
  EXPECT_EQ(0, this->chunk_store_->Count(name_mem));
  EXPECT_EQ(0, this->chunk_store_->Size(name_mem));
  EXPECT_FALSE(this->chunk_store_->Has(name_file));
  EXPECT_EQ(0, this->chunk_store_->Count(name_file));
  EXPECT_EQ(0, this->chunk_store_->Size(name_file));

  // store from string
  EXPECT_TRUE(this->chunk_store_->Store(name_mem, content));
  EXPECT_FALSE(this->chunk_store_->Empty());
  EXPECT_EQ(1, this->chunk_store_->Count());
  EXPECT_EQ(123, this->chunk_store_->Size());
  EXPECT_TRUE(this->chunk_store_->Has(name_mem));
  EXPECT_EQ(1, this->chunk_store_->Count(name_mem));
  EXPECT_EQ(123, this->chunk_store_->Size(name_mem));
  EXPECT_FALSE(this->chunk_store_->Has(name_file));
  EXPECT_EQ(0, this->chunk_store_->Count(name_file));
  EXPECT_EQ(0, this->chunk_store_->Size(name_file));

  ASSERT_EQ(name_mem.string(),
            crypto::Hash<crypto::SHA512>(this->chunk_store_->Get(name_mem)).string());

  // store from file
  EXPECT_TRUE(this->chunk_store_->Store(name_file, path, false));
  EXPECT_FALSE(this->chunk_store_->Empty());
  EXPECT_EQ(2, this->chunk_store_->Count());
  EXPECT_EQ(579, this->chunk_store_->Size());
  EXPECT_TRUE(this->chunk_store_->Has(name_mem));
  EXPECT_EQ(1, this->chunk_store_->Count(name_mem));
  EXPECT_EQ(123, this->chunk_store_->Size(name_mem));
  EXPECT_TRUE(this->chunk_store_->Has(name_file));
  EXPECT_EQ(1, this->chunk_store_->Count(name_file));
  EXPECT_EQ(456, this->chunk_store_->Size(name_file));

  ASSERT_EQ(name_file.string(),
            crypto::Hash<crypto::SHA512>(this->chunk_store_->Get(name_file)).string());

  fs::path new_path(*this->test_dir_ / "chunk2.dat");
  this->CreateRandomFile(new_path, 333);
  ChunkId new_name(crypto::HashFile<crypto::SHA512>(new_path));

  // overwrite existing
  EXPECT_TRUE(this->chunk_store_->Store(name_mem, NonEmptyString(RandomString(222))));
  EXPECT_TRUE(this->chunk_store_->Store(name_file, "", false));
  EXPECT_TRUE(this->chunk_store_->Store(name_file, new_path, false));
  EXPECT_FALSE(this->chunk_store_->Empty());
  EXPECT_EQ(2, this->chunk_store_->Count());
  EXPECT_EQ(579, this->chunk_store_->Size());
  EXPECT_TRUE(this->chunk_store_->Has(name_mem));
  EXPECT_EQ(2, this->chunk_store_->Count(name_mem));
  EXPECT_EQ(123, this->chunk_store_->Size(name_mem));
  EXPECT_TRUE(this->chunk_store_->Has(name_file));
  EXPECT_EQ(3, this->chunk_store_->Count(name_file));
  EXPECT_EQ(456, this->chunk_store_->Size(name_file));

  ASSERT_EQ(name_mem.string(),
            crypto::Hash<crypto::SHA512>(this->chunk_store_->Get(name_mem)).string());
  ASSERT_EQ(name_file.string(),
            crypto::Hash<crypto::SHA512>(this->chunk_store_->Get(name_file)).string());

  // delete input file (existing chunk)
  EXPECT_THROW(this->chunk_store_->Store(ChunkId(), path, true), std::exception);
  EXPECT_TRUE(fs::exists(path));
  EXPECT_TRUE(this->chunk_store_->Store(name_mem, path, true));
  EXPECT_FALSE(fs::exists(path));

  // delete input file (new chunk)
  EXPECT_TRUE(this->chunk_store_->Store(new_name, new_path, true));
  EXPECT_EQ(new_name.string(),
            crypto::Hash<crypto::SHA512>(this->chunk_store_->Get(new_name)).string());
  EXPECT_FALSE(fs::exists(path));
  EXPECT_FALSE(this->chunk_store_->Store(new_name, new_path, true));
  EXPECT_FALSE(this->chunk_store_->Empty());
  EXPECT_EQ(3, this->chunk_store_->Count());
  EXPECT_EQ(912, this->chunk_store_->Size());
  EXPECT_TRUE(this->chunk_store_->Has(new_name));
  EXPECT_EQ(1, this->chunk_store_->Count(new_name));
  EXPECT_EQ(333, this->chunk_store_->Size(new_name));
}

TYPED_TEST_P(ChunkStoreTest, BEH_RepeatedStore) {
  NonEmptyString content1(RandomString(123)), content2(RandomString(123));
  ChunkId name_mem1(crypto::Hash<crypto::SHA512>(content1));
  ChunkId name_mem2(crypto::Hash<crypto::SHA512>(content2));

  for (uintmax_t i(1); i <= 80; ++i) {
    EXPECT_TRUE(this->chunk_store_->Store(name_mem1, content1));
    EXPECT_FALSE(this->chunk_store_->Empty());
    EXPECT_EQ(1, this->chunk_store_->Count());
    EXPECT_EQ(123, this->chunk_store_->Size());
    EXPECT_TRUE(this->chunk_store_->Has(name_mem1));
  }

  EXPECT_TRUE(this->chunk_store_->Delete(name_mem1));
  EXPECT_EQ(1, this->chunk_store_->Count());
  EXPECT_EQ(123, this->chunk_store_->Size());
  EXPECT_TRUE(this->chunk_store_->Has(name_mem1));

  EXPECT_TRUE(this->chunk_store_->Store(name_mem2, content1));
  EXPECT_FALSE(this->chunk_store_->Empty());
  EXPECT_EQ(2, this->chunk_store_->Count());
  EXPECT_EQ(246, this->chunk_store_->Size());
  EXPECT_TRUE(this->chunk_store_->Has(name_mem2));
  EXPECT_TRUE(this->chunk_store_->Has(name_mem1));
}

TYPED_TEST_P(ChunkStoreTest, BEH_Delete) {
  NonEmptyString content(RandomString(123));
  ChunkId name_mem(crypto::Hash<crypto::SHA512>(content));
  fs::path path(*this->test_dir_ / "chunk.dat");
  this->CreateRandomFile(path, 456);
  ChunkId name_file(crypto::HashFile<crypto::SHA512>(path));
  ASSERT_NE(name_mem, name_file);

  // invalid input
  EXPECT_THROW(this->chunk_store_->Delete(ChunkId()), std::exception);

  // non-existing chunk
  EXPECT_TRUE(this->chunk_store_->Delete(name_mem));

  ASSERT_TRUE(this->chunk_store_->Store(name_mem, content));
  ASSERT_TRUE(this->chunk_store_->Store(name_file, path, true));

  EXPECT_FALSE(this->chunk_store_->Empty());
  EXPECT_EQ(2, this->chunk_store_->Count());
  EXPECT_EQ(579, this->chunk_store_->Size());
  EXPECT_TRUE(this->chunk_store_->Has(name_mem));
  EXPECT_EQ(1, this->chunk_store_->Count(name_mem));
  EXPECT_EQ(123, this->chunk_store_->Size(name_mem));
  EXPECT_TRUE(this->chunk_store_->Has(name_file));
  EXPECT_EQ(1, this->chunk_store_->Count(name_file));
  EXPECT_EQ(456, this->chunk_store_->Size(name_file));

  // Delete existing chunks
  EXPECT_TRUE(this->chunk_store_->Delete(name_file));
  EXPECT_FALSE(this->chunk_store_->Has(name_file));
  EXPECT_EQ(0, this->chunk_store_->Count(name_file));
  EXPECT_EQ(0, this->chunk_store_->Size(name_file));
  EXPECT_TRUE(this->chunk_store_->Get(name_file).empty());
  EXPECT_EQ(1, this->chunk_store_->Count());
  EXPECT_EQ(123, this->chunk_store_->Size());
  EXPECT_TRUE(this->chunk_store_->Delete(name_mem));
  EXPECT_FALSE(this->chunk_store_->Has(name_mem));
  EXPECT_EQ(0, this->chunk_store_->Count(name_mem));
  EXPECT_EQ(0, this->chunk_store_->Size(name_mem));
  EXPECT_TRUE(this->chunk_store_->Get(name_mem).empty());

  EXPECT_TRUE(this->chunk_store_->Empty());
  EXPECT_EQ(0, this->chunk_store_->Count());
  EXPECT_EQ(0, this->chunk_store_->Size());
}

TYPED_TEST_P(ChunkStoreTest, BEH_Modify) {
  NonEmptyString content(RandomString(123));
  ChunkId non_hash_name(RandomString(65));  // Non Hashable Name
  ChunkId hash_name(crypto::Hash<crypto::SHA512>(content));  // Hash Name
  fs::path path(*this->test_dir_ / "chunk.dat");
  this->CreateRandomFile(path, 456);
  ChunkId name_file(RandomString(65));
  ChunkId hash_name_file(crypto::HashFile<crypto::SHA512>(path));
  ASSERT_NE(non_hash_name, name_file);
  /* Random File Data with more content than original */
  NonEmptyString modified_content(RandomString(125));
  fs::path empty_path;
  fs::path modified_path(*this->test_dir_ / "chunk-modified.dat");
  this->CreateRandomFile(modified_path, 460);
  /* Random File Data with lesser content than mod-1 */
  NonEmptyString modified_content2(RandomString(120));
  fs::path modified_path2(*this->test_dir_ / "chunk-modified2.dat");
  this->CreateRandomFile(modified_path2, 455);

  // Store Initial Chunks and Verify Store Operation
  ASSERT_TRUE(this->chunk_store_->Store(non_hash_name, content));
  ASSERT_TRUE(this->chunk_store_->Store(hash_name, content));
  ASSERT_TRUE(this->chunk_store_->Store(name_file, path, false));

  EXPECT_FALSE(this->chunk_store_->Empty());
  EXPECT_EQ(3, this->chunk_store_->Count());
  EXPECT_EQ(702, this->chunk_store_->Size());
  EXPECT_TRUE(this->chunk_store_->Has(non_hash_name));
  EXPECT_EQ(1, this->chunk_store_->Count(non_hash_name));
  EXPECT_EQ(123, this->chunk_store_->Size(non_hash_name));
  EXPECT_TRUE(this->chunk_store_->Has(hash_name));
  EXPECT_EQ(1, this->chunk_store_->Count(hash_name));
  EXPECT_EQ(123, this->chunk_store_->Size(hash_name));
  EXPECT_TRUE(this->chunk_store_->Has(name_file));
  EXPECT_EQ(1, this->chunk_store_->Count(name_file));
  EXPECT_EQ(456, this->chunk_store_->Size(name_file));

  // Invalid Calls to Modify
  EXPECT_THROW(this->chunk_store_->Modify(ChunkId(), modified_content), std::exception);
  EXPECT_THROW(this->chunk_store_->Modify(ChunkId(), modified_path, false), std::exception);
  EXPECT_FALSE(this->chunk_store_->Modify(name_file, empty_path, false));

  // Making the Store Full and test Calls to Modify
  if (std::string(typeid(TypeParam).name()).find("Buffered") == std::string::npos &&
      std::string(typeid(TypeParam).name()).find("File") == std::string::npos) {
    this->chunk_store_->SetCapacity(702);
    EXPECT_FALSE(this->chunk_store_->Modify(non_hash_name, modified_content));
    EXPECT_FALSE(this->chunk_store_->Modify(name_file, modified_path, false));

    // Check Modify on Hash Chunk Returns False
    EXPECT_FALSE(this->chunk_store_->Modify(hash_name, modified_content));
  }
  EXPECT_TRUE(this->chunk_store_->Modify(hash_name, modified_content2));
  EXPECT_EQ(1, this->chunk_store_->Count(hash_name));

  // Setting Free Space in Storage
  this->chunk_store_->SetCapacity(1024);

  // Valid Calls on Non-Reference Count Store
  /* Mod Procedure - 1 */
  EXPECT_TRUE(this->chunk_store_->Modify(non_hash_name, modified_content));
  EXPECT_TRUE(this->chunk_store_->Modify(name_file, modified_path, false));
  EXPECT_TRUE(this->chunk_store_->Has(non_hash_name));
  EXPECT_EQ(1, this->chunk_store_->Count(non_hash_name));
  EXPECT_EQ(125, this->chunk_store_->Size(non_hash_name));
  EXPECT_TRUE(this->chunk_store_->Has(hash_name));
  EXPECT_EQ(1, this->chunk_store_->Count(hash_name));
  EXPECT_EQ(120, this->chunk_store_->Size(hash_name));
  EXPECT_TRUE(this->chunk_store_->Has(name_file));
  EXPECT_EQ(1, this->chunk_store_->Count(name_file));
  EXPECT_EQ(460, this->chunk_store_->Size(name_file));
  EXPECT_EQ(3, this->chunk_store_->Count());
  EXPECT_EQ(705, this->chunk_store_->Size());

  /* Mod Procedure - 2 */
  EXPECT_TRUE(this->chunk_store_->Modify(non_hash_name, modified_content2));
  EXPECT_TRUE(this->chunk_store_->Modify(name_file, modified_path2, false));
  EXPECT_TRUE(this->chunk_store_->Has(non_hash_name));
  EXPECT_EQ(1, this->chunk_store_->Count(non_hash_name));
  EXPECT_EQ(120, this->chunk_store_->Size(non_hash_name));
  EXPECT_TRUE(this->chunk_store_->Has(name_file));
  EXPECT_EQ(1, this->chunk_store_->Count(name_file));
  EXPECT_EQ(455, this->chunk_store_->Size(name_file));
  EXPECT_EQ(695, this->chunk_store_->Size());

  EXPECT_TRUE(this->chunk_store_->Delete(non_hash_name));
  EXPECT_TRUE(this->chunk_store_->Delete(hash_name));
  EXPECT_TRUE(this->chunk_store_->Delete(name_file));

  // Setting up Reference Count Store Chunks and Verifying
  this->chunk_store_->SetCapacity(2048);
  ASSERT_TRUE(this->chunk_store_->Store(hash_name, content));
  EXPECT_TRUE(this->chunk_store_->Store(hash_name, content));
  ASSERT_TRUE(this->chunk_store_->Store(hash_name_file, path, false));
  EXPECT_TRUE(this->chunk_store_->Store(hash_name_file, path, false));
  EXPECT_TRUE(this->chunk_store_->Store(non_hash_name, content));
  EXPECT_TRUE(this->chunk_store_->Store(name_file, path, true));
  ASSERT_FALSE(fs::exists(path));
  EXPECT_TRUE(this->chunk_store_->Has(hash_name));
  EXPECT_EQ(2, this->chunk_store_->Count(hash_name));
  EXPECT_TRUE(this->chunk_store_->Has(hash_name_file));
  EXPECT_EQ(2, this->chunk_store_->Count(hash_name_file));
  EXPECT_TRUE(this->chunk_store_->Has(name_file));
  EXPECT_EQ(1, this->chunk_store_->Count(name_file));
  EXPECT_TRUE(this->chunk_store_->Has(non_hash_name));
  EXPECT_EQ(1, this->chunk_store_->Count(non_hash_name));
  EXPECT_EQ(1158, this->chunk_store_->Size());

  // Check Modify on Hash Chunk Returns True but doesn't increase count
  EXPECT_TRUE(this->chunk_store_->Modify(hash_name, modified_content));
  EXPECT_EQ(2, this->chunk_store_->Count(hash_name));
  EXPECT_EQ(125, this->chunk_store_->Size(hash_name));
  EXPECT_TRUE(this->chunk_store_->Modify(hash_name_file, modified_path, false));
  EXPECT_EQ(2, this->chunk_store_->Count(hash_name_file));

  // Valid Calls on Reference Count Store
  /* Mod Procedure - 1 */
  this->chunk_store_->SetCapacity(2048);
  EXPECT_TRUE(this->chunk_store_->Modify(non_hash_name, modified_content));
  EXPECT_TRUE(this->chunk_store_->Modify(name_file, modified_path, true));
  ASSERT_FALSE(fs::exists(modified_path));
  EXPECT_TRUE(this->chunk_store_->Has(non_hash_name));
  EXPECT_EQ(1, this->chunk_store_->Count(non_hash_name));
  EXPECT_EQ(125, this->chunk_store_->Size(non_hash_name));
  EXPECT_TRUE(this->chunk_store_->Has(name_file));
  EXPECT_EQ(1, this->chunk_store_->Count(name_file));
  EXPECT_EQ(460, this->chunk_store_->Size(name_file));
  EXPECT_EQ(1170, this->chunk_store_->Size());

  /* Mod Procedure - 2 */
  EXPECT_TRUE(this->chunk_store_->Modify(non_hash_name, modified_content2));
  EXPECT_TRUE(this->chunk_store_->Modify(name_file, modified_path2, true));
  ASSERT_FALSE(fs::exists(modified_path2));
  EXPECT_TRUE(this->chunk_store_->Has(non_hash_name));
  EXPECT_EQ(1, this->chunk_store_->Count(non_hash_name));
  EXPECT_EQ(120, this->chunk_store_->Size(non_hash_name));
  EXPECT_TRUE(this->chunk_store_->Has(name_file));
  EXPECT_EQ(1, this->chunk_store_->Count(name_file));
  EXPECT_EQ(455, this->chunk_store_->Size(name_file));
  EXPECT_EQ(1160, this->chunk_store_->Size());
}

TYPED_TEST_P(ChunkStoreTest, BEH_MoveTo) {
  NonEmptyString content1(RandomString(100));
  ChunkId name1(crypto::Hash<crypto::SHA512>(content1));
  NonEmptyString content2(RandomString(50));
  ChunkId name2(crypto::Hash<crypto::SHA512>(content2));
  NonEmptyString content3(RandomString(25));
  ChunkId name3(crypto::Hash<crypto::SHA512>(content3));

  // ( | )  ->  (1 2 | 2 3)
  EXPECT_TRUE(this->chunk_store_->Store(name1, content1));
  EXPECT_TRUE(this->chunk_store_->Store(name2, content2));
  EXPECT_EQ(2, this->chunk_store_->Count());
  EXPECT_EQ(150, this->chunk_store_->Size());
  EXPECT_TRUE(this->alt_chunk_store_->Store(name2, content2));
  EXPECT_TRUE(this->alt_chunk_store_->Store(name3, content3));
  EXPECT_EQ(2, this->alt_chunk_store_->Count());
  EXPECT_EQ(75, this->alt_chunk_store_->Size());

  // (1 2 | 2 3)  ->  (1 | 2 3)
  EXPECT_TRUE(this->chunk_store_->MoveTo(name2, this->alt_chunk_store_.get()));
  EXPECT_FALSE(this->chunk_store_->Has(name2));
  EXPECT_EQ(0, this->chunk_store_->Count(name2));
  EXPECT_EQ(0, this->chunk_store_->Size(name2));
  EXPECT_EQ(1, this->chunk_store_->Count());
  EXPECT_EQ(100, this->chunk_store_->Size());
  EXPECT_TRUE(this->alt_chunk_store_->Has(name2));
  EXPECT_EQ(2, this->alt_chunk_store_->Count(name2));
  EXPECT_EQ(50, this->alt_chunk_store_->Size(name2));
  EXPECT_EQ(2, this->alt_chunk_store_->Count());
  EXPECT_EQ(75, this->alt_chunk_store_->Size());

  // (1 | 2 3)  ->  (1 2 | 3)
  EXPECT_TRUE(this->alt_chunk_store_->MoveTo(name2, this->chunk_store_.get()));
  EXPECT_TRUE(this->chunk_store_->Has(name2));
  EXPECT_EQ(1, this->chunk_store_->Count(name2));
  EXPECT_EQ(50, this->chunk_store_->Size(name2));
  EXPECT_EQ(2, this->chunk_store_->Count());
  EXPECT_EQ(150, this->chunk_store_->Size());
  EXPECT_TRUE(this->alt_chunk_store_->Has(name2));
  EXPECT_EQ(1, this->alt_chunk_store_->Count(name2));
  EXPECT_EQ(50, this->alt_chunk_store_->Size(name2));
  EXPECT_EQ(2, this->alt_chunk_store_->Count());
  EXPECT_EQ(75, this->alt_chunk_store_->Size());

  // (1 2 | 3)  ->  (1 2 3 | )
  EXPECT_TRUE(this->alt_chunk_store_->MoveTo(name3, this->chunk_store_.get()));
  EXPECT_TRUE(this->chunk_store_->Has(name3));
  EXPECT_EQ(1, this->chunk_store_->Count(name3));
  EXPECT_EQ(25, this->chunk_store_->Size(name3));
  EXPECT_EQ(3, this->chunk_store_->Count());
  EXPECT_EQ(175, this->chunk_store_->Size());
  EXPECT_FALSE(this->alt_chunk_store_->Has(name3));
  EXPECT_EQ(0, this->alt_chunk_store_->Count(name3));
  EXPECT_EQ(0, this->alt_chunk_store_->Size(name3));
  EXPECT_EQ(1, this->alt_chunk_store_->Count());
  EXPECT_EQ(50, this->alt_chunk_store_->Size());
  EXPECT_FALSE(this->alt_chunk_store_->Empty());

  // failures
  EXPECT_FALSE(this->alt_chunk_store_->MoveTo(name1, this->chunk_store_.get()));
  EXPECT_THROW(this->chunk_store_->MoveTo(ChunkId(), this->alt_chunk_store_.get()), std::exception);
  EXPECT_FALSE(this->chunk_store_->MoveTo(name1, nullptr));
}

TYPED_TEST_P(ChunkStoreTest, BEH_Capacity) {
  if (std::string(typeid(TypeParam).name()).find("Buffered") != std::string::npos ||
      std::string(typeid(TypeParam).name()).find("File") != std::string::npos) {
    return;
  }

  NonEmptyString content1(RandomString(100));
  ChunkId name1(crypto::Hash<crypto::SHA512>(content1));
  NonEmptyString content2(RandomString(50));
  ChunkId name2(crypto::Hash<crypto::SHA512>(content2));
  NonEmptyString content3(RandomString(25));
  ChunkId name3(crypto::Hash<crypto::SHA512>(content3));

  EXPECT_EQ(0, this->chunk_store_->Capacity());
  EXPECT_TRUE(this->chunk_store_->Vacant(0));
  EXPECT_TRUE(this->chunk_store_->Vacant(123456789));
  this->chunk_store_->SetCapacity(125);
  EXPECT_EQ(125, this->chunk_store_->Capacity());
  EXPECT_TRUE(this->chunk_store_->Vacant(125));
  EXPECT_FALSE(this->chunk_store_->Vacant(126));

  // store #1, space to 100
  EXPECT_TRUE(this->chunk_store_->Vacant(content1.string().size()));
  EXPECT_TRUE(this->chunk_store_->Store(name1, content1));
  EXPECT_EQ(100, this->chunk_store_->Size());

  // try storing #2, 25 over limit
  EXPECT_FALSE(this->chunk_store_->Vacant(content2.string().size()));
  EXPECT_FALSE(this->chunk_store_->Store(name2, content2));
  EXPECT_EQ(100, this->chunk_store_->Size());

  // store #3, space to 125, which equals limit
  EXPECT_TRUE(this->chunk_store_->Vacant(content3.string().size()));
  EXPECT_TRUE(this->chunk_store_->Store(name3, content3));
  EXPECT_EQ(125, this->chunk_store_->Size());

  this->chunk_store_->SetCapacity(150);

  // try storing #2, again 25 over limit
  EXPECT_FALSE(this->chunk_store_->Vacant(content2.string().size()));
  EXPECT_FALSE(this->chunk_store_->Store(name2, content2));
  EXPECT_EQ(125, this->chunk_store_->Size());

  // delete #3, space to 100
  EXPECT_TRUE(this->chunk_store_->Delete(name3));
  EXPECT_EQ(100, this->chunk_store_->Size());

  // store #2, space to 150, which equals limit
  EXPECT_TRUE(this->chunk_store_->Vacant(content2.string().size()));
  EXPECT_TRUE(this->chunk_store_->Store(name2, content2));
  EXPECT_EQ(150, this->chunk_store_->Size());

  // store #1 again, nothing changes
  EXPECT_FALSE(this->chunk_store_->Vacant(content1.string().size()));
  EXPECT_TRUE(this->chunk_store_->Store(name1, content1));
  EXPECT_EQ(150, this->chunk_store_->Size());

  // can't reduce capacity as space is taken
  EXPECT_EQ(150, this->chunk_store_->Capacity());
  this->chunk_store_->SetCapacity(125);
  EXPECT_EQ(150, this->chunk_store_->Capacity());

  EXPECT_TRUE(this->alt_chunk_store_->Store(name1, content1));
  EXPECT_TRUE(this->alt_chunk_store_->Store(name3, content3));

  // moving #1 succeeds since it already exists
  EXPECT_FALSE(this->chunk_store_->Vacant(content1.string().size()));
  EXPECT_TRUE(this->alt_chunk_store_->MoveTo(name1, this->chunk_store_.get()));
  EXPECT_FALSE(this->alt_chunk_store_->Has(name1));
  EXPECT_EQ(3, this->chunk_store_->Count(name1));

  // moving #3 fails since we are full
  EXPECT_FALSE(this->chunk_store_->Vacant(content3.string().size()));
  EXPECT_FALSE(this->alt_chunk_store_->MoveTo(name3, this->chunk_store_.get()));
  EXPECT_FALSE(this->chunk_store_->Has(name3));
  EXPECT_TRUE(this->alt_chunk_store_->Has(name3));

  // delete #1, space to 50
  EXPECT_TRUE(this->chunk_store_->Delete(name1));
  EXPECT_TRUE(this->chunk_store_->Delete(name1));
  EXPECT_TRUE(this->chunk_store_->Delete(name1));
  EXPECT_EQ(50, this->chunk_store_->Size());

  // moving #3 succeeds now
  EXPECT_TRUE(this->chunk_store_->Vacant(content3.string().size()));
  EXPECT_TRUE(this->alt_chunk_store_->MoveTo(name3, this->chunk_store_.get()));
  EXPECT_TRUE(this->chunk_store_->Has(name3));
  EXPECT_FALSE(this->alt_chunk_store_->Has(name3));
  EXPECT_EQ(75, this->chunk_store_->Size());

  // reducing capacity succeeds now
  EXPECT_EQ(150, this->chunk_store_->Capacity());
  this->chunk_store_->SetCapacity(125);
  EXPECT_EQ(125, this->chunk_store_->Capacity());

  fs::path path(*this->test_dir_ / "chunk.dat");
  this->CreateRandomFile(path, 100);
  ChunkId name_file(crypto::HashFile<crypto::SHA512>(path));

  // try storing file, 50 over limit
  EXPECT_FALSE(this->chunk_store_->Vacant(100));
  EXPECT_FALSE(this->chunk_store_->Store(name_file, path, false));
  EXPECT_FALSE(this->chunk_store_->Has(name_file));
  EXPECT_EQ(75, this->chunk_store_->Size());

  this->chunk_store_->Clear();

  // store file again, succeeds now
  EXPECT_TRUE(this->chunk_store_->Store(name_file, path, false));
  EXPECT_TRUE(this->chunk_store_->Has(name_file));
  EXPECT_EQ(100, this->chunk_store_->Size());
}

TYPED_TEST_P(ChunkStoreTest, BEH_References) {
  NonEmptyString content1(RandomString(100));
  ChunkId name1(crypto::Hash<crypto::SHA512>(content1));
  NonEmptyString content2(RandomString(50));
  ChunkId name2(crypto::Hash<crypto::SHA512>(content2));
  fs::path path(*this->test_dir_ / "chunk.dat");
  this->CreateRandomFile(path, 25);
  ChunkId name3(crypto::HashFile<crypto::SHA512>(path));

  // test failures
  EXPECT_THROW(this->chunk_store_->Get(ChunkId()), std::exception);
  EXPECT_TRUE(this->chunk_store_->Get(name1).empty());
  EXPECT_THROW(this->chunk_store_->Get(ChunkId(), *this->test_dir_ / "dummy"), std::exception);
  EXPECT_FALSE(this->chunk_store_->Get(name1, fs::path("")));
  EXPECT_FALSE(this->chunk_store_->Get(name1, *this->test_dir_ / "dummy"));
  EXPECT_THROW(this->chunk_store_->Store(ChunkId(), NonEmptyString("dummy")), std::exception);
  EXPECT_THROW(this->chunk_store_->Store(name1, NonEmptyString()), std::exception);
  EXPECT_THROW(this->chunk_store_->Store(ChunkId(), path, false), std::exception);
  EXPECT_THROW(this->chunk_store_->Delete(ChunkId()), std::exception);
  EXPECT_THROW(this->chunk_store_->MoveTo(ChunkId(), this->chunk_store_.get()), std::exception);
  EXPECT_FALSE(this->chunk_store_->MoveTo(name1, this->tiger_chunk_store_.get()));
  EXPECT_THROW(this->chunk_store_->Has(ChunkId()), std::exception);
  EXPECT_FALSE(this->chunk_store_->Has(name1));
  EXPECT_THROW(this->chunk_store_->Count(ChunkId()), std::exception);
  EXPECT_EQ(0, this->chunk_store_->Count(name1));
  EXPECT_THROW(this->chunk_store_->Size(ChunkId()), std::exception);
  EXPECT_EQ(0, this->chunk_store_->Size(name1));

  // add chunk twice, reference counting enabled
  EXPECT_TRUE(this->chunk_store_->Store(name1, content1));
  EXPECT_TRUE(this->chunk_store_->Has(name1));
  EXPECT_EQ(1, this->chunk_store_->Count(name1));
  EXPECT_EQ(100, this->chunk_store_->Size(name1));
  EXPECT_EQ(content1.string(), this->chunk_store_->Get(name1));
  EXPECT_EQ(100, this->chunk_store_->Size());
  EXPECT_EQ(1, this->chunk_store_->Count());
  EXPECT_TRUE(this->chunk_store_->Store(name1, content1 + content1));
  EXPECT_TRUE(this->chunk_store_->Has(name1));
  EXPECT_EQ(2, this->chunk_store_->Count(name1));
  EXPECT_EQ(100, this->chunk_store_->Size(name1));
  EXPECT_EQ(100, this->chunk_store_->Size());
  EXPECT_EQ(1, this->chunk_store_->Count());
  EXPECT_TRUE(this->chunk_store_->Delete(name1));
  EXPECT_TRUE(this->chunk_store_->Has(name1));
  EXPECT_EQ(1, this->chunk_store_->Count(name1));
  EXPECT_EQ(100, this->chunk_store_->Size(name1));
  EXPECT_EQ(100, this->chunk_store_->Size());
  EXPECT_EQ(1, this->chunk_store_->Count());
  EXPECT_FALSE(this->chunk_store_->Empty());
  EXPECT_TRUE(this->chunk_store_->Delete(name1));
  EXPECT_FALSE(this->chunk_store_->Has(name1));
  EXPECT_EQ(0, this->chunk_store_->Count(name1));
  EXPECT_EQ(0, this->chunk_store_->Size(name1));
  EXPECT_EQ(0, this->chunk_store_->Size());
  EXPECT_EQ(0, this->chunk_store_->Count());
  EXPECT_TRUE(this->chunk_store_->Empty());

  // adding from file
  EXPECT_TRUE(this->chunk_store_->Store(name3, path, false));
  EXPECT_EQ(1, this->chunk_store_->Count(name3));
  EXPECT_TRUE(this->chunk_store_->Store(name3, path, true));
  EXPECT_EQ(2, this->chunk_store_->Count(name3));
  EXPECT_TRUE(this->chunk_store_->Store(name3, content1));
  EXPECT_EQ(3, this->chunk_store_->Count(name3));

  this->chunk_store_->Clear();

  // adding via move
  EXPECT_TRUE(this->alt_chunk_store_->Store(name2, content2));
  EXPECT_TRUE(this->alt_chunk_store_->MoveTo(name2, this->chunk_store_.get()));
  EXPECT_FALSE(this->alt_chunk_store_->Has(name2));
  EXPECT_TRUE(this->chunk_store_->Has(name2));
  EXPECT_EQ(content2.string(), this->chunk_store_->Get(name2));
  EXPECT_EQ(1, this->chunk_store_->Count(name2));
  EXPECT_TRUE(this->alt_chunk_store_->Store(name2, content2));
  EXPECT_TRUE(this->alt_chunk_store_->MoveTo(name2, this->chunk_store_.get()));
  EXPECT_FALSE(this->alt_chunk_store_->Has(name2));
  EXPECT_EQ(2, this->chunk_store_->Count(name2));
  if (std::string(typeid(TypeParam).name()).find("Buffered") == std::string::npos &&
      std::string(typeid(TypeParam).name()).find("File") == std::string::npos) {
    this->alt_chunk_store_->SetCapacity(10);
    EXPECT_FALSE(this->chunk_store_->MoveTo(name2, this->alt_chunk_store_.get()));
    EXPECT_FALSE(this->alt_chunk_store_->Has(name2));
    EXPECT_TRUE(this->chunk_store_->Has(name2));
    EXPECT_EQ(2, this->chunk_store_->Count(name2));
  }
  this->alt_chunk_store_->SetCapacity(0);
  EXPECT_TRUE(this->chunk_store_->MoveTo(name2, this->alt_chunk_store_.get()));
  EXPECT_TRUE(this->alt_chunk_store_->Has(name2));
  EXPECT_EQ(1, this->alt_chunk_store_->Count(name2));
  EXPECT_TRUE(this->chunk_store_->Has(name2));
  EXPECT_EQ(1, this->chunk_store_->Count(name2));
  EXPECT_TRUE(this->chunk_store_->MoveTo(name2, this->alt_chunk_store_.get()));
  EXPECT_EQ(0, this->chunk_store_->Count(name2));
  EXPECT_EQ(2, this->alt_chunk_store_->Count(name2));
  EXPECT_TRUE(this->alt_chunk_store_->Has(name2));
  EXPECT_FALSE(this->chunk_store_->Has(name2));
  EXPECT_TRUE(this->chunk_store_->Empty());
  EXPECT_FALSE(this->chunk_store_->MoveTo(name2, this->alt_chunk_store_.get()));

  // multiple chunks
  uintmax_t n1((RandomUint32() % 5) + 1), n2((RandomUint32() % 5) + 1);
  this->chunk_store_->SetCapacity(150);
  for (uintmax_t i = 0; i < n1; ++i)
    EXPECT_TRUE(this->chunk_store_->Store(name1, content1));
  for (uintmax_t i = 0; i < n2; ++i)
    EXPECT_TRUE(this->chunk_store_->Store(name2, content2));
  EXPECT_TRUE(this->chunk_store_->Has(name1));
  EXPECT_TRUE(this->chunk_store_->Has(name2));
  EXPECT_EQ(n1, this->chunk_store_->Count(name1));
  EXPECT_EQ(n2, this->chunk_store_->Count(name2));
  EXPECT_EQ(100, this->chunk_store_->Size(name1));
  EXPECT_EQ(50, this->chunk_store_->Size(name2));
  EXPECT_EQ(150, this->chunk_store_->Size());
  EXPECT_EQ(2, this->chunk_store_->Count());
}

TYPED_TEST_P(ChunkStoreTest, BEH_Clear) {
  std::vector<ChunkId> chunks;
  for (int i = 0; i < 20; ++i) {
    NonEmptyString content(RandomString(100));
    ChunkId name(crypto::Hash<crypto::SHA512>(content));
    chunks.push_back(name);
    EXPECT_TRUE(this->chunk_store_->Store(name, content));
    EXPECT_TRUE(this->chunk_store_->Has(name));
  }
  EXPECT_FALSE(this->chunk_store_->Empty());
  EXPECT_EQ(20, this->chunk_store_->Count());
  EXPECT_EQ(2000, this->chunk_store_->Size());

  this->chunk_store_->Clear();

  for (auto it = chunks.begin(); it != chunks.end(); ++it)
    EXPECT_FALSE(this->chunk_store_->Has(*it));
  EXPECT_TRUE(this->chunk_store_->Empty());
  EXPECT_EQ(0, this->chunk_store_->Count());
  EXPECT_EQ(0, this->chunk_store_->Size());
}

TYPED_TEST_P(ChunkStoreTest, BEH_GetChunks) {
  std::vector<std::pair<ChunkId, NonEmptyString>> chunks;
  for (int i = 0; i < 100; ++i) {
    NonEmptyString content(RandomString(100 + (i % 20)));
    ChunkId name(crypto::Hash<crypto::SHA512>(content));
    chunks.push_back(std::make_pair(name, content));
  }

  for (auto it = chunks.begin(); it != chunks.end(); ++it) {
    EXPECT_TRUE(this->chunk_store_->Store(it->first, it->second));
    EXPECT_EQ(this->chunk_store_->Size(it->first), it->second.string().size());
  }

  EXPECT_EQ(100, this->chunk_store_->Count());

  std::vector<ChunkData> chunk_data(this->chunk_store_->GetChunks());
  EXPECT_EQ(100, chunk_data.size());

  int chunks_found(0);

  for (auto it = chunk_data.begin(); it != chunk_data.end(); ++it) {
    for (auto chunks_it = chunks.begin(); chunks_it != chunks.end(); ++chunks_it) {
      if (chunks_it->first == it->chunk_name) {
        EXPECT_EQ(chunks_it->second.string().size(), it->chunk_size);
        ++chunks_found;
        break;
      }
    }
  }

  EXPECT_EQ(100, chunks_found);
}

REGISTER_TYPED_TEST_CASE_P(ChunkStoreTest,
                           BEH_Init,
                           BEH_Get,
                           BEH_Store,
                           BEH_RepeatedStore,
                           BEH_Delete,
                           BEH_Modify,
                           BEH_MoveTo,
                           BEH_Capacity,
                           BEH_References,
                           BEH_Clear,
                           BEH_GetChunks);

}  // namespace test

}  // namespace chunk_store

}  // namespace priv

}  // namespace maidsafe

#endif  // MAIDSAFE_PRIVATE_CHUNK_STORE_TESTS_CHUNK_STORE_API_TEST_H_
