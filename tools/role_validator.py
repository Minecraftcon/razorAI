#!/usr/bin/env python3
import os
import sys
import glob

try:
    import yaml
except ImportError:
    print("\033[31m[ERROR] The 'pyyaml' library is not installed.\033[0m")
    print("Please install it by running: pip install pyyaml")
    sys.exit(1)

ROLES_DIR = os.path.expanduser("~/.razor/roles/")

# ANSI Colors
RESET = "\033[0m"
RED = "\033[31m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
BLUE = "\033[34m"
CYAN = "\033[36m"
BOLD = "\033[1m"

VALID_PARAMS = {
    "roleName", 
    "description",
    "sysPrompt", 
    "executionPolicy", 
    "tools", 
    "executionHierarchy", 
    "loopbackLimit", 
    "handoverOnFailure",
    "handoverOnSuccess",
    "multiModel"
}

REQUIRED_PARAMS = {
    "roleName",
    "sysPrompt"
}

def validate_roles():
    print(f"{CYAN}{BOLD}========================================{RESET}")
    print(f"{CYAN}{BOLD}       Razor Role YAML Validator        {RESET}")
    print(f"{CYAN}{BOLD}========================================{RESET}")

    if not os.path.exists(ROLES_DIR):
        print(f"{RED}[ERROR] Roles directory '{ROLES_DIR}' does not exist!{RESET}")
        return False

    yaml_files = glob.glob(os.path.join(ROLES_DIR, "*.yaml")) + glob.glob(os.path.join(ROLES_DIR, "*.yml"))
    
    if not yaml_files:
        print(f"{YELLOW}[WARNING] No YAML files found in '{ROLES_DIR}'.{RESET}")
        print("Please create your roles based on the schema in roles.md.")
        return True

    roles_data = {}
    has_errors = False
    hierarchies = {}
    
    # Pass 1: Parse and validate individual file schemas
    for file_path in yaml_files:
        filename = os.path.basename(file_path)
        try:
            with open(file_path, "r") as f:
                data = yaml.safe_load(f)
        except Exception as e:
            print(f"{RED}[FAIL]{RESET} {filename}: YAML Parsing Error: {e}")
            has_errors = True
            continue
            
        if not isinstance(data, dict):
            print(f"{RED}[FAIL]{RESET} {filename}: File is empty or not a valid YAML dictionary.")
            has_errors = True
            continue

        role_name = data.get("roleName", f"UNKNOWN_{filename}")
        roles_data[role_name] = {"file": filename, "data": data}
        
        file_errors = []
        
        # Check required params
        for req in REQUIRED_PARAMS:
            if req not in data:
                file_errors.append(f"Missing required parameter '{req}'")

        # Check unrecognized params
        for key in data.keys():
            if key not in VALID_PARAMS:
                file_errors.append(f"Unrecognized parameter '{key}'")
                
        # Track hierarchy for global validation
        if "executionHierarchy" in data:
            hier = data["executionHierarchy"]
            if not isinstance(hier, int):
                file_errors.append(f"'executionHierarchy' must be an integer, got {type(hier).__name__}")
            else:
                if hier in hierarchies:
                    hierarchies[hier].append(filename)
                else:
                    hierarchies[hier] = [filename]
                    
        # Check specific types
        if "executionPolicy" in data and data["executionPolicy"] not in ["direct", "review", "loop"]:
            file_errors.append(f"Invalid executionPolicy: '{data['executionPolicy']}'. Must be 'direct', 'review', or 'loop'")
            
        if "loopbackLimit" in data and not isinstance(data["loopbackLimit"], int):
            file_errors.append("'loopbackLimit' must be an integer")
            
        if "sysPrompt" in data and not isinstance(data["sysPrompt"], (list, str)):
            file_errors.append("'sysPrompt' must be a list of strings or a single block string")

        if "multiModel" in data and not isinstance(data["multiModel"], bool):
            file_errors.append("'multiModel' must be a boolean (true/false)")

        if file_errors:
            print(f"{RED}[FAIL]{RESET} {filename}")
            for err in file_errors:
                print(f"       {RED}->{RESET} {err}")
            has_errors = True
        else:
            print(f"{GREEN}[ OK ]{RESET} {filename} (Role: {role_name})")

    print(f"\n{CYAN}{BOLD}--- Global Cross-Role Validation ---{RESET}")
    
    # Pass 2: Global validatons (Hierarchy collisions & Handover targets)
    
    # Hierarchy collision check
    for hier, files in hierarchies.items():
        if len(files) > 1:
            print(f"{RED}[FAIL]{RESET} Hierarchy Collision! executionHierarchy {hier} is used by multiple roles: {', '.join(files)}")
            has_errors = True
            
    # Handover targets check
    for role_name, info in roles_data.items():
        data = info["data"]
        filename = info["file"]
        
        if "handoverOnFailure" in data:
            target = data["handoverOnFailure"]
            if target not in roles_data:
                print(f"{RED}[FAIL]{RESET} {filename}: 'handoverOnFailure' targets '{target}', but no such roleName exists across the loaded YAMLs!")
                has_errors = True
            elif target == role_name:
                print(f"{RED}[FAIL]{RESET} {filename}: 'handoverOnFailure' cannot target itself (infinite loop).")
                has_errors = True

    if has_errors:
        print(f"\n{RED}{BOLD}Validation Failed. Please fix the errors above.{RESET}")
        sys.exit(1)
    else:
        print(f"\n{GREEN}{BOLD}All Roles Validated Successfully!{RESET}")
        sys.exit(0)

if __name__ == "__main__":
    validate_roles()
