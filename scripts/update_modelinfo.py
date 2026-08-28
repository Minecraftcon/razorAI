#!/usr/bin/env python3
import os
import sys
import yaml
import json
import logging

logging.basicConfig(level=logging.INFO, format='%(levelname)s: %(message)s')

def get_max_ctx(model_id):
    # Hardcoded known max contexts for efficiency and offline capability.
    model_id = model_id.lower()
    
    # Gemini
    if "gemini" in model_id:
        return 1048576  # 1M tokens
    
    # Mistral
    if "codestral-latest" in model_id or "codestral-mamba" in model_id:
        return 256000
    if "mistral-large" in model_id:
        return 131072
    if "nemo" in model_id or "mistral-small" in model_id or "ministral" in model_id:
        return 128000
    if "codestral-22b" in model_id:
        return 32768

    # Nvidia / Nemotron
    if "nemotron" in model_id:
        if "120b" in model_id:
            return 131072 # Nemotron 3 Super 120B
        return 131072 # Assume max for other Nemotrons

    # OpenAI / OSS
    if "gpt-oss-120b" in model_id:
        return 131072
    if "gpt-oss-20b" in model_id:
        return 32768

    # Grok
    if "grok" in model_id:
        return 131072

    # Kimi
    if "kimi" in model_id:
        return 200000
        
    # OpenCode
    if "opencode" in model_id:
        return 131072
        
    # Default fallback
    return 32768

def main():
    home_dir = os.path.expanduser("~")
    razor_dir = os.path.join(home_dir, ".razor")
    model_yaml_path = os.path.join(razor_dir, "model.yaml")
    modelinfo_json_path = os.path.join(razor_dir, "modelinfo.json")

    # If model.yaml doesn't exist in ~/.razor, try local fallback
    if not os.path.exists(model_yaml_path):
        model_yaml_path = "model.yaml"
        if not os.path.exists(model_yaml_path):
            logging.error(f"Cannot find model.yaml at {model_yaml_path}")
            sys.exit(1)

    # Read model.yaml
    with open(model_yaml_path, 'r') as f:
        try:
            config = yaml.safe_load(f)
        except Exception as e:
            logging.error(f"Failed to parse {model_yaml_path}: {e}")
            sys.exit(1)

    yaml_models = config.get('models', [])
    yaml_model_names = [m.get('name') for m in yaml_models if 'name' in m]

    # Read existing modelinfo.json
    modelinfo = {}
    if os.path.exists(modelinfo_json_path):
        try:
            with open(modelinfo_json_path, 'r') as f:
                modelinfo = json.load(f)
        except Exception as e:
            logging.warning(f"Failed to parse {modelinfo_json_path}, creating fresh. Error: {e}")
            modelinfo = {}

    current_info_names = list(modelinfo.keys())
    
    # Check if they are completely in sync
    if set(yaml_model_names) == set(current_info_names):
        logging.info("modelinfo.json is in sync with model.yaml. No updates needed.")
        sys.exit(0)

    # They differ. Let's sync.
    logging.info("Differences detected. Syncing modelinfo.json...")

    new_modelinfo = {}
    for model in yaml_models:
        name = model.get('name')
        if not name:
            continue
            
        model_id = model.get('model', '')
        
        # Keep existing info if it exists, otherwise fetch
        if name in modelinfo:
            new_modelinfo[name] = modelinfo[name]
        else:
            logging.info(f"Adding new model: {name}")
            new_modelinfo[name] = {
                "max_ctx": get_max_ctx(model_id)
            }

    # Write back to modelinfo.json
    try:
        os.makedirs(razor_dir, exist_ok=True)
        with open(modelinfo_json_path, 'w') as f:
            json.dump(new_modelinfo, f, indent=4)
        logging.info(f"Successfully updated {modelinfo_json_path}")
    except Exception as e:
        logging.error(f"Failed to write {modelinfo_json_path}: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
