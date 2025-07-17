import os
import sys
import json
import traceback
import torch
from dotenv import load_dotenv
from transformers import AutoTokenizer, AutoModelForCausalLM

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from utils import read_file
from llms.base_classifier import IAMClassifier

load_dotenv()

class HuggingFaceClassifier(IAMClassifier):
    def __init__(self):
        super().__init__()
        self.model_id = os.environ.get("HUGGINGFACE_MODEL_ID")
        self.device = "cuda" if torch.cuda.is_available() else "cpu"

        self.tokenizer = AutoTokenizer.from_pretrained(self.model_id)
        self.model = AutoModelForCausalLM.from_pretrained(self.model_id).to(self.device)

        self.system_instruction = read_file(os.environ["LLM_INITIAL_PROMPT_PATH"])

    def test_connection(self) -> bool:
        try:
            messages = [{"role": "user", "content": "Ping"}]
            prompt = self.tokenizer.apply_chat_template(
                messages, tokenize=False, add_generation_prompt=True
            )
            inputs = self.tokenizer(prompt, return_tensors="pt").to(self.device)
            output = self.model.generate(**inputs, max_new_tokens=20)
            decoded = self.tokenizer.decode(output[0][inputs.input_ids.shape[1]:], skip_special_tokens=True)
            print("Hugging Face local model connection successful.")
            print("Response:", decoded)
            return True
        except Exception as e:
            print(f"Hugging Face connection failed: {e}")
            traceback.print_exc()
            return False

    def classify_policy(self, policy_json: dict) -> dict | None:
        user_prompt = f"Here is the IAM policy:\n{json.dumps(policy_json, indent=2)}\nReturn only the JSON output."
        messages = [
            {"role": "system", "content": self.system_instruction},
            {"role": "user", "content": user_prompt}
        ]

        try:
            prompt = self.tokenizer.apply_chat_template(messages, tokenize=False, add_generation_prompt=True)
            inputs = self.tokenizer(prompt, return_tensors="pt").to(self.device)
            output = self.model.generate(**inputs, max_new_tokens=500, temperature=0.2)
            decoded = self.tokenizer.decode(output[0][inputs.input_ids.shape[1]:], skip_special_tokens=True)
            return json.loads(decoded[decoded.find("{"):])
        except Exception as e:
            print(f"Error during classification: {e}")
            traceback.print_exc()
            return None

if __name__ == "__main__":
    HuggingFaceClassifier().main()
