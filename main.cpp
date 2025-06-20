#include <iostream> 
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

void init() {
    if (!fs::exists(".minigit")) {
        fs::create_directory(".minigit");
        fs::create_directory(".minigit/objects");
        fs::create_directory(".minigit/commits");
        fs::create_directory(".minigit/refs");
        fs::create_directory(".minigit/refs/heads");

        std::ofstream head_file(fs::path(".minigit/HEAD"));
        if(head_file.is_open()){
            head_file << "ref: refs/heads/master";
            head_file.close();
        }else{
            std::cerr<<"Error: Could not open .minigit/HEAD for writing.\n";
            return;
        }

        std::cout << "Initialized empty MiniGit repository in .minigit/\n";
    } else {
        std::cout << "MiniGit is already initialized.\n";
    }
}

void checkout(const std::string& target) {
    std::string commitHash;

    std::ifstream branchFile(".minigit/refs/heads/" + target);
    if (branchFile.is_open()) {
        std::getline(branchFile, commitHash);
        branchFile.close();

        std::ofstream headFile(".minigit/HEAD");
        headFile << "ref: refs/heads/" << target;
        headFile.close();
    } else {
        commitHash = target;
        std::ofstream headFile(".minigit/HEAD");
        headFile << commitHash;
        headFile.close();
    }

    std::ifstream commitFile(".minigit/commits/" + commitHash);
    if (!commitFile.is_open()) {
        std::cerr << "Error: Commit not found: " << commitHash << "\n";
        return;
    }

    std::string line;
    while (std::getline(commitFile, line)) {
        size_t space = line.find(' ');
        if (space == std::string::npos) continue;

        std::string filename = line.substr(0, space);
        std::string blobHash = line.substr(space + 1);

        std::ifstream blob(".minigit/objects/" + blobHash);
        if (!blob.is_open()) {
            std::cerr << "Error: Cannot find blob " << blobHash << "\n";
            continue;
        }

        std::ofstream out(filename);
        std::string content;
        while (std::getline(blob, content)) {
            out << content << "\n";
        }

        blob.close();
        out.close();
        std::cout << "Restored " << filename << "\n";
    }

    commitFile.close();
    std::cout << "✅ Checkout completed for: " << target << "\n";
}

int main() {
    std::string command;

    while (true) {
        std::cout << "Enter command (init, checkout <branch/hash>, exit): ";
        std::cin >> command;

        if (command == "init") {
            init();
        } else if (command == "checkout") {
            std::string target;
            std::cin >> target;
            checkout(target);
        } else if (command == "exit") {
            std::cout << "Exiting MiniGit.\n";
            break;
        } else {
            std::cout << "Unknown command.\n";
        }
    }
    return 0;
} 