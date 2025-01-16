// SPDX-License-Identifier: Apache-2.0

#include "manager.hpp"
#include "mock_ext_data_ifaces.hpp"

#include <sdbusplus/async/context.hpp>

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

class ManagerTest : public ::testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        char tmpdir[] = "/tmp/pdsCfgDirXXXXXX";
        dataSyncCfgDir = mkdtemp(tmpdir);
    }

    // Set up each individual test
    void SetUp() override
    {
        dataSyncCfgFile = dataSyncCfgDir / "config.json";
        // Ensure the config file does not exist before we start
        if (std::filesystem::exists(dataSyncCfgFile))
        {
            std::filesystem::remove(dataSyncCfgFile);
        }
    }

    // Tear down each individual test
    void TearDown() override
    {
        // Remove each item from the directory
        for (const auto& entry :
             std::filesystem::directory_iterator(dataSyncCfgDir))
        {
            std::filesystem::remove_all(entry.path());
        }
    }

    void writeConfig(const nlohmann::json& jsonData)
    {
        std::ofstream cfgFile(dataSyncCfgFile);
        if (cfgFile.is_open())
        {
            cfgFile << jsonData;
        }
    }

    static void writeData(const std::string& filename, const std::string& data)
    {
        std::ofstream out(filename);
        out << data;
        out.close();
    }

    static std::string readData(const std::string& filename)
    {
        std::ifstream in(filename);
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        in.close();
        return content;
    }

    static void TearDownTestSuite()
    {
        std::filesystem::remove_all(dataSyncCfgDir);
        std::filesystem::remove(dataSyncCfgDir);
    }

    static std::filesystem::path dataSyncCfgDir;
    std::filesystem::path dataSyncCfgFile;
};

std::filesystem::path ManagerTest::dataSyncCfgDir;

TEST_F(ManagerTest, ParseDataSyncCfg)
{
    using namespace std::literals;
    namespace ed = data_sync::ext_data;

    std::unique_ptr<ed::ExternalDataIFaces> extDataIface =
        std::make_unique<ed::MockExternalDataIFaces>();

    ed::MockExternalDataIFaces* mockExtDataIfaces =
        dynamic_cast<ed::MockExternalDataIFaces*>(extDataIface.get());

    EXPECT_CALL(*mockExtDataIfaces, fetchBMCRedundancyMgrProps())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    EXPECT_CALL(*mockExtDataIfaces, fetchSiblingBmcIP())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    EXPECT_CALL(*mockExtDataIfaces, fetchRbmcCredentials())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = R"(
                {
                    "Files": [
                        {
                            "Path": "/file/path/to/sync",
                            "Description": "Parse test file",
                            "SyncDirection": "Active2Passive",
                            "SyncType": "Immediate"
                        }
                    ],
                    "Directories": [
                        {
                            "Path": "/directory/path/to/sync",
                            "Description": "Parse test directory",
                            "SyncDirection": "Passive2Active",
                            "SyncType": "Periodic",
                            "Periodicity": "PT5M",
                            "RetryAttempts": 1,
                            "RetryInterval": "PT10M",
                            "ExcludeFilesList": ["/directory/file/to/ignore"],
                            "IncludeFilesList": ["/directory/file/to/consider"]
                        }
                    ]
                }
            )"_json;

    writeConfig(jsonData);

    sdbusplus::async::context ctx;

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    EXPECT_FALSE(manager.containsDataSyncCfg(jsonData["Files"][0]));

    ctx.spawn(
        sdbusplus::async::sleep_for(ctx, 1ns) |
        sdbusplus::async::execution::then([&ctx]() { ctx.request_stop(); }));
    ctx.run();

    EXPECT_TRUE(manager.containsDataSyncCfg(jsonData["Files"][0]));
}

TEST_F(ManagerTest, PeriodicDataSyncTest)
{
    using namespace std::literals;
    namespace ed = data_sync::ext_data;

    std::unique_ptr<ed::ExternalDataIFaces> extDataIface =
        std::make_unique<ed::MockExternalDataIFaces>();

    ed::MockExternalDataIFaces* mockExtDataIfaces =
        dynamic_cast<ed::MockExternalDataIFaces*>(extDataIface.get());

    EXPECT_CALL(*mockExtDataIfaces, fetchBMCRedundancyMgrProps())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    EXPECT_CALL(*mockExtDataIfaces, fetchSiblingBmcIP())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    EXPECT_CALL(*mockExtDataIfaces, fetchRbmcCredentials())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = R"(
                {
                    "Files": [
                        {
                            "Path": "file1",
                            "DestinationPath": "testfile",
                            "Description": "Parse test file",
                            "SyncDirection": "Bidirectional",
                            "SyncType": "Periodic",
                            "Periodicity": "PT2S"
                        }
                    ]
                }
            )"_json;

    std::string filename{ManagerTest::dataSyncCfgDir.string() + "/" +
                         jsonData["Files"][0]["Path"].get<std::string>()};
    std::string destfilename{
        ManagerTest::dataSyncCfgDir.string() + "/" +
        jsonData["Files"][0]["DestinationPath"].get<std::string>()};
    jsonData["Files"][0]["Path"] = filename;
    jsonData["Files"][0]["DestinationPath"] = destfilename;

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string data{"Initial Data\n"};
    ManagerTest::writeData(filename, data);

    ASSERT_EQ(ManagerTest::readData(filename), data);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    EXPECT_NE(ManagerTest::readData(destfilename), data);

    ctx.spawn(
        sdbusplus::async::sleep_for(ctx, 3s) |
        sdbusplus::async::execution::then([&ctx]() { ctx.request_stop(); }));
    ctx.run();

    EXPECT_EQ(ManagerTest::readData(destfilename), data);

    // ctx is stopped, modified data should not sync to dest path
    std::string updated_data{"Data got updated\n"};
    ManagerTest::writeData(filename, updated_data);
    ASSERT_EQ(ManagerTest::readData(filename), updated_data);

    EXPECT_NE(ManagerTest::readData(destfilename), updated_data);
}

TEST_F(ManagerTest, PeriodicDataSyncMultiRWTest)
{
    using namespace std::literals;
    namespace ed = data_sync::ext_data;

    std::unique_ptr<ed::ExternalDataIFaces> extDataIface =
        std::make_unique<ed::MockExternalDataIFaces>();

    ed::MockExternalDataIFaces* mockExtDataIfaces =
        dynamic_cast<ed::MockExternalDataIFaces*>(extDataIface.get());

    EXPECT_CALL(*mockExtDataIfaces, fetchBMCRedundancyMgrProps())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    EXPECT_CALL(*mockExtDataIfaces, fetchSiblingBmcIP())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    EXPECT_CALL(*mockExtDataIfaces, fetchRbmcCredentials())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = R"(
                {
                    "Files": [
                        {
                            "Path": "file1",
                            "DestinationPath": "testfile",
                            "Description": "Parse test file",
                            "SyncDirection": "Bidirectional",
                            "SyncType": "Periodic",
                            "Periodicity": "PT1S"
                        }
                    ]
                }
            )"_json;

    std::string filename{ManagerTest::dataSyncCfgDir.string() + "/" +
                         jsonData["Files"][0]["Path"].get<std::string>()};
    std::string destfilename{
        ManagerTest::dataSyncCfgDir.string() + "/" +
        jsonData["Files"][0]["DestinationPath"].get<std::string>()};
    jsonData["Files"][0]["Path"] = filename;
    jsonData["Files"][0]["DestinationPath"] = destfilename;

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string data{"Initial Data\n"};
    ManagerTest::writeData(filename, data);

    ASSERT_EQ(ManagerTest::readData(filename), data);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};

    EXPECT_NE(ManagerTest::readData(destfilename), data);

    std::string updated_data{"Data got updated\n"};
    ManagerTest::writeData(filename, updated_data);
    EXPECT_NE(ManagerTest::readData(destfilename), updated_data);

    ctx.spawn(
        sdbusplus::async::sleep_for(ctx, 3s) |
        sdbusplus::async::execution::then([&ctx]() { ctx.request_stop(); }));
    ctx.run();

    EXPECT_EQ(ManagerTest::readData(destfilename), updated_data);
}

TEST_F(ManagerTest, PeriodicDataSyncP2ATest)
{
    using namespace std::literals;
    namespace ed = data_sync::ext_data;

    std::unique_ptr<ed::ExternalDataIFaces> extDataIface =
        std::make_unique<ed::MockExternalDataIFaces>();

    ed::MockExternalDataIFaces* mockExtDataIfaces =
        dynamic_cast<ed::MockExternalDataIFaces*>(extDataIface.get());

    EXPECT_CALL(*mockExtDataIfaces, fetchBMCRedundancyMgrProps())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    EXPECT_CALL(*mockExtDataIfaces, fetchSiblingBmcIP())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    EXPECT_CALL(*mockExtDataIfaces, fetchRbmcCredentials())
        // NOLINTNEXTLINE
        .WillRepeatedly([]() -> sdbusplus::async::task<> { co_return; });

    nlohmann::json jsonData = R"(
                {
                    "Files": [
                        {
                            "Path": "file1",
                            "DestinationPath": "testfile",
                            "Description": "Parse test file",
                            "SyncDirection": "Passive2Active",
                            "SyncType": "Periodic",
                            "Periodicity": "PT1S"
                        }
                    ]
                }
            )"_json;

    std::string filename{ManagerTest::dataSyncCfgDir.string() + "/" +
                         jsonData["Files"][0]["Path"].get<std::string>()};
    std::string destfilename{
        ManagerTest::dataSyncCfgDir.string() + "/" +
        jsonData["Files"][0]["DestinationPath"].get<std::string>()};
    jsonData["Files"][0]["Path"] = filename;
    jsonData["Files"][0]["DestinationPath"] = destfilename;

    writeConfig(jsonData);
    sdbusplus::async::context ctx;

    std::string data{"Initial Data\n"};
    ManagerTest::writeData(filename, data);

    ASSERT_EQ(ManagerTest::readData(filename), data);
    EXPECT_NE(ManagerTest::readData(destfilename), data);

    data_sync::Manager manager{ctx, std::move(extDataIface),
                               ManagerTest::dataSyncCfgDir};
    ctx.spawn(
        sdbusplus::async::sleep_for(ctx, 1ns) |
        sdbusplus::async::execution::then([&ctx]() { ctx.request_stop(); }));
    ctx.run();

    // As the sync direction is from Passive to Active, the data should not get
    // updated
    EXPECT_NE(ManagerTest::readData(destfilename), data);
}
