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
using namespace httplib;

class Task {
public:
    int id;
    std::string title;
    std::string description;
    std::string status;
    std::string create_time;
    std::string update_time;

    Task() : id(0), status("todo") {}

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
        return tasks.size();
    }
};

bool isStatusValid(const std::string& status) {
    return status == "todo" || status == "in_progress" || status == "done";
}

class TodoAPI {
private:
    Server svr;
    TaskStorage taskStorage;
    int port = 8080;
    bool checkJsonTasks(const json& body, json& error) {
        if (!body.contains("title") || body["title"].empty()) {
            error = { {"error", "Title is required"} };
            return false;
        }

        if (body.contains("status")) {
            std::string status = body["status"];
            if (!isStatusValid(status)) {
                error = {
                    {"error", "Invalid status"},
                    {"valid_statuses", {"todo", "in_progress", "done"}}
                };
                return false;
            }
        }

        return true;
    }
public:
    TodoAPI(int port = 8080) : port(port) {
        setupEndpoints();
    }

    void setupEndpoints() {
        svr.set_pre_routing_handler([](const Request& req, Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");
            return Server::HandlerResponse::Unhandled;
            });

        svr.Options(".*", [](const Request& req, Response& res) {
            res.status = 200;
            });

        svr.Get("/status", [this](const Request& req, Response& res) {
            json response = {
                {"status", "ok"},
                {"tasks_count", taskStorage.count()},
                {"service", "Todo API"}
            };
            res.set_content(response.dump(), "application/json");
            });

        svr.Get("/tasks", [this](const Request& req, Response& res) {
            try {
                auto tasks = taskStorage.getAllTasks();
                std::cout << "Tasks count: " << tasks.size() << std::endl;

                json response = json::array();
                for (const auto& task : tasks) {
                    response.push_back(task.toJson());
                }
                res.set_content(response.dump(), "application/json");
            }
            catch (const std::exception& e) {
                std::cerr << "error in GET /tasks: " << e.what() << std::endl;
                res.status = 500;
                res.set_content(
                    json{ {"error", "Internal Server Error"}, {"details", e.what()} }.dump(),
                    "application/json"
                );
            }
            });

        svr.Get("/tasks/(\\d+)", [this](const Request& req, Response& res) {
            int id = std::stoi(req.matches[1]);
            Task task = taskStorage.getTask(id);

            if (task.id != 0) {
                res.set_content(task.toJson().dump(), "application/json");
            }
            else {
                res.status = 404;
                json error = { {"error", "Task not found"}, {"id", id} };
                res.set_content(error.dump(), "application/json");
            }
            });

        svr.Post("/tasks", [this](const Request& req, Response& res) {
            try {
                json body = json::parse(req.body);
                json validationError;

                if (!checkJsonTasks(body, validationError)) {
                    res.status = 400;
                    res.set_content(validationError.dump(), "application/json");
                    return;
                }
                Task newTask = Task::fromJson(body);
                Task created = taskStorage.createTask(newTask);

                res.status = 201;
                res.set_content(created.toJson().dump(), "application/json");

            }
            catch (const json::parse_error& e) {
                res.status = 400;
                json error = { {"error", "Invalid JSON format"}, {"details", e.what()} };
                res.set_content(error.dump(), "application/json");
            }
            });

        svr.Put("/tasks/(\\d+)", [this](const Request& req, Response& res) {
            try {
                int id = std::stoi(req.matches[1]);
                json body = json::parse(req.body);
                json validationError;

                if (!checkJsonTasks(body, validationError)) {
                    res.status = 400;
                    res.set_content(validationError.dump(), "application/json");
                    return;
                }
                Task updatedTask = Task::fromJson(body);
                if (taskStorage.updateTask(id, updatedTask)) {
                    Task task = taskStorage.getTask(id);
                    res.set_content(task.toJson().dump(), "application/json");
                }
                else {
                    res.status = 404;
                    json error = { {"error", "Task not found"}, {"id", id} };
                    res.set_content(error.dump(), "application/json");
                }

            }
            catch (const json::parse_error& e) {
                res.status = 400;
                json error = { {"error", "Invalid JSON format"} };
                res.set_content(error.dump(), "application/json");
            }
            });

        svr.Patch("/tasks/(\\d+)", [this](const Request& req, Response& res) {
            try {
                int id = std::stoi(req.matches[1]);
                json body = json::parse(req.body);

                if (body.empty()) {
                    res.status = 400;
                    json error = { {"error", "No fields to update"} };
                    res.set_content(error.dump(), "application/json");
                    return;
                }
                if (body.contains("status")) {
                    std::string status = body["status"];
                    if (!isStatusValid(status)) {
                        res.status = 400;
                        json error = {
                            {"error", "Invalid status"},
                            {"valid_statuses", {"todo", "in_progress", "done"}}
                        };
                        res.set_content(error.dump(), "application/json");
                        return;
                    }
                }
                if (taskStorage.patchTask(id, body)) {
                    Task task = taskStorage.getTask(id);
                    res.set_content(task.toJson().dump(), "application/json");
                }
                else {
                    res.status = 404;
                    json error = { {"error", "Task not found"}, {"id", id} };
                    res.set_content(error.dump(), "application/json");
                }

            }
            catch (const json::parse_error& e) {
                res.status = 400;
                json error = { {"error", "Invalid JSON format"} };
                res.set_content(error.dump(), "application/json");
            }
            });

        svr.Delete("/tasks/(\\d+)", [this](const Request& req, Response& res) {
            int id = std::stoi(req.matches[1]);

            if (taskStorage.deleteTask(id)) {
                res.status = 204;
            }
            else {
                res.status = 404;
                json error = { {"error", "Task not found"}, {"id", id} };
                res.set_content(error.dump(), "application/json");
            }
            });
    }

    void run() {
        std::cout << "________________________________________" << std::endl;
        std::cout << "Todo API Server" << std::endl;
        std::cout << "Port: " << port << std::endl;
        std::cout << "________________________________________" << std::endl;
        std::cout << "Endpoints:" << std::endl;
        std::cout << "  GET    /status         - API status" << std::endl;
        std::cout << "  GET    /tasks          - Get all tasks" << std::endl;
        std::cout << "  GET    /tasks/{id}     - Get task by ID" << std::endl;
        std::cout << "  POST   /tasks          - Create new task" << std::endl;
        std::cout << "  PUT    /tasks/{id}     - Update task" << std::endl;
        std::cout << "  PATCH  /tasks/{id}     - Partially update task" << std::endl;
        std::cout << "  DELETE /tasks/{id}     - Delete task" << std::endl;
        std::cout << "________________________________________" << std::endl;

        initialize();

        svr.listen("0.0.0.0", port);
    }

    void initialize() {
        taskStorage.createTask(Task(0, "Buy milk", "Fat 3.2%", "todo"));
        taskStorage.createTask(Task(0, "Run API", "Configure and start server", "in_progress"));
        taskStorage.createTask(Task(0, "Explore Postman", "Check REST API", "done"));
    }
};

int main() {
    try {
        TodoAPI api(8080);
        api.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}