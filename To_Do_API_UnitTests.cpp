#include "pch.h"
#include "CppUnitTest.h"
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <ctime>
#include <sstream>
#include <iomanip>

#include "nlohmann/json.hpp"
#include "httplib.h"

using json = nlohmann::json;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace TestCode {
    class Task {
    public:
        int id;
        std::string title;
        std::string description;
        std::string status;
        std::string create_time;
        std::string update_time;

        Task() : id(0), status("todo") {
            setTime();
        }

        Task(int id, const std::string& title, const std::string& description, const std::string& status)
            : id(id), title(title), description(description), status(status) {
            setTime();
        }

        void setTime() {
            auto t = std::time(nullptr);
            std::tm tm;
            localtime_s(&tm, &t);
            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
            create_time = oss.str();
            update_time = create_time;
        }

        void updateTime() {
            auto t = std::time(nullptr);
            std::tm tm;
            localtime_s(&tm, &t);
            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
            update_time = oss.str();
        }

        json toJson() const {
            try {
                return json{
                    {"id", id},
                    {"title", title},
                    {"description", description},
                    {"status", status},
                    {"create_time", create_time},
                    {"update_time", update_time}
                };
            }
            catch (const std::exception& e) {
                std::cerr << "error in Task::toJson: " << e.what() << std::endl;
                return json{ {"error", "Failed to serialize task"} };
            }
        }

        static Task fromJson(const json& j) {
            Task task;
            if (j.contains("title")) task.title = j["title"];
            if (j.contains("description")) task.description = j["description"];
            if (j.contains("status")) task.status = j["status"];
            return task;
        }
    };

    class TaskStorage {
    private:
        std::map<int, Task> tasks;
        int nextId = 1;
        mutable std::mutex mtx;

    public:
        Task createTask(const Task& task) {
            std::lock_guard<std::mutex> lock(mtx);
            Task newTask = task;
            newTask.id = nextId++;
            newTask.setTime();
            tasks[newTask.id] = newTask;
            return newTask;
        }

        std::vector<Task> getAllTasks() const {
            std::lock_guard<std::mutex> lock(mtx);
            std::vector<Task> result;
            for (const auto& pair : tasks) {
                result.push_back(pair.second);
            }
            return result;
        }

        Task getTask(int id) const {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = tasks.find(id);
            if (it != tasks.end()) {
                return it->second;
            }
            return Task();
        }

        bool updateTask(int id, const Task& updatedTask) {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = tasks.find(id);
            if (it != tasks.end()) {
                Task& task = it->second;
                task.title = updatedTask.title;
                task.description = updatedTask.description;
                task.status = updatedTask.status;
                task.updateTime();
                return true;
            }
            return false;
        }

        bool patchTask(int id, const json& updates) {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = tasks.find(id);
            if (it != tasks.end()) {
                Task& task = it->second;
                if (updates.contains("title")) {
                    task.title = updates["title"];
                }
                if (updates.contains("description")) {
                    task.description = updates["description"];
                }
                if (updates.contains("status")) {
                    task.status = updates["status"];
                }
                task.updateTime();
                return true;
            }
            return false;
        }

        bool deleteTask(int id) {
            std::lock_guard<std::mutex> lock(mtx);
            return tasks.erase(id) > 0;
        }

        size_t count() const {
            std::lock_guard<std::mutex> lock(mtx);
            return tasks.size();
        }
    };

    bool isStatusValid(const std::string& status) {
        return status == "todo" || status == "in_progress" || status == "done";
    }
}

using namespace TestCode;

namespace ToDoAPIUnitTests
{
    TEST_CLASS(TaskTests)
    {
    public:

        TEST_METHOD(TestDefaultConstructor)
        {
            Task task;

            Assert::AreEqual(0, task.id);
            Assert::AreEqual(std::string(""), task.title);
            Assert::AreEqual(std::string(""), task.description);
            Assert::AreEqual(std::string("todo"), task.status);
            Assert::IsFalse(task.create_time.empty());
            Assert::IsFalse(task.update_time.empty());
        }

        TEST_METHOD(TestParameterConstructor)
        {
            Task task(1, "Test Title", "Test Description", "in_progress");

            Assert::AreEqual(1, task.id);
            Assert::AreEqual(std::string("Test Title"), task.title);
            Assert::AreEqual(std::string("Test Description"), task.description);
            Assert::AreEqual(std::string("in_progress"), task.status);
            Assert::IsFalse(task.create_time.empty());
            Assert::IsFalse(task.update_time.empty());
            Assert::AreEqual(task.create_time, task.update_time);
        }
        TEST_METHOD(TestToJson)
        {
            Task task(42, "Json Test", "Json Description", "done");

            json j = task.toJson();

            Assert::AreEqual(42, j["id"].get<int>());
            Assert::AreEqual(std::string("Json Test"), j["title"].get<std::string>());
            Assert::AreEqual(std::string("Json Description"), j["description"].get<std::string>());
            Assert::AreEqual(std::string("done"), j["status"].get<std::string>());
            Assert::IsTrue(j.contains("create_time"));
            Assert::IsTrue(j.contains("update_time"));
        }

        TEST_METHOD(TestFromJson)
        {
            json j = {
                {"title", "New Task"},
                {"description", "Task Description"},
                {"status", "in_progress"}
            };

            Task task = Task::fromJson(j);

            Assert::AreEqual(std::string("New Task"), task.title);
            Assert::AreEqual(std::string("Task Description"), task.description);
            Assert::AreEqual(std::string("in_progress"), task.status);
            Assert::AreEqual(0, task.id);
        }

        TEST_METHOD(TestFromJsonPartial)
        {
            json j = {
                {"title", "Partial Task"}
            };

            Task task = Task::fromJson(j);

            Assert::AreEqual(std::string("Partial Task"), task.title);
            Assert::AreEqual(std::string(""), task.description);
            Assert::AreEqual(std::string("todo"), task.status);
        }
    };

    TEST_CLASS(TaskStorageTests)
    {
    public:

        TEST_METHOD(TestCreateTask)
        {
            TaskStorage storage;
            Task task(0, "Test Task", "Test Description", "todo");

            Task created = storage.createTask(task);

            Assert::IsTrue(created.id > 0);
            Assert::AreEqual(std::string("Test Task"), created.title);
            Assert::AreEqual(std::string("todo"), created.status);
        }

        TEST_METHOD(TestGetTask)
        {
            TaskStorage storage;
            Task task(0, "Test", "Description", "todo");
            Task created = storage.createTask(task);

            Task task1 = storage.getTask(created.id);

            Assert::AreEqual(created.id, task1.id);
            Assert::AreEqual(std::string("Test"), task1.title);
            Assert::AreEqual(std::string("Description"), task1.description);
        }

        TEST_METHOD(TestGetNonExistentTask)
        {
            TaskStorage storage;

            Task task = storage.getTask(999);

            Assert::AreEqual(0, task.id);
        }

        TEST_METHOD(TestGetAllTasks)
        {
            TaskStorage storage;

            storage.createTask(Task(0, "Task 1", "Desc 1", "todo"));
            storage.createTask(Task(0, "Task 2", "Desc 2", "in_progress"));
            storage.createTask(Task(0, "Task 3", "Desc 3", "done"));

            auto tasks = storage.getAllTasks();

            Assert::AreEqual(static_cast<size_t>(3), tasks.size());
            Assert::AreEqual(std::string("Task 1"), tasks[0].title);
            Assert::AreEqual(std::string("Task 2"), tasks[1].title);
            Assert::AreEqual(std::string("Task 3"), tasks[2].title);
        }

        TEST_METHOD(TestUpdateTask)
        {
            TaskStorage storage;
            Task original(0, "Original", "Original Desc", "todo");
            Task created = storage.createTask(original);

            Task updated(0, "Updated", "Updated Desc", "in_progress");
            bool success = storage.updateTask(created.id, updated);

            Assert::IsTrue(success);

            Task task = storage.getTask(created.id);
            Assert::AreEqual(std::string("Updated"), task.title);
            Assert::AreEqual(std::string("Updated Desc"), task.description);
            Assert::AreEqual(std::string("in_progress"), task.status);
        }

        TEST_METHOD(TestUpdateNonExistentTask)
        {
            TaskStorage storage;
            Task task(0, "Test", "Desc", "todo");

            bool success = storage.updateTask(999, task);

            Assert::IsFalse(success);
        }

        TEST_METHOD(TestDeleteTask)
        {
            TaskStorage storage;
            Task task(0, "Test", "Desc", "todo");
            Task created = storage.createTask(task);

            bool deleted = storage.deleteTask(created.id);

            Assert::IsTrue(deleted);

            Task task1 = storage.getTask(created.id);
            Assert::AreEqual(0, task.id);
        }

        TEST_METHOD(TestStorageCount)
        {
            TaskStorage storage;

            Assert::AreEqual(static_cast<size_t>(0), storage.count());

            storage.createTask(Task(0, "Task 1", "Desc 1", "todo"));
            Assert::AreEqual(static_cast<size_t>(1), storage.count());

            storage.createTask(Task(0, "Task 2", "Desc 2", "in_progress"));
            Assert::AreEqual(static_cast<size_t>(2), storage.count());
        }
    };

    TEST_CLASS(ValidationTests)
    {
    public:

        TEST_METHOD(TestIsStatusValid)
        {
            Assert::IsTrue(isStatusValid("todo"));
            Assert::IsTrue(isStatusValid("in_progress"));
            Assert::IsTrue(isStatusValid("done"));

            Assert::IsFalse(isStatusValid(""));
            Assert::IsFalse(isStatusValid("invalid"));
        }
    };
}