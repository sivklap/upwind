import os
import json
def read_JSON_file(file_path):
    """
    Reads a JSON file from the given file path.

    Args:
       file_path (str): The path to the json file.

    Returns:
        dict or None: The JSON content of the file as a dictionary if successful.
                      Returns None if the file is empty, not found, or not valid JSON.

    Prints:
        An error message if the file is empty, not found, or contains invalid JSON.
    """

    try:
        with open(file_path, "r") as f:
            content = f.read()
            if not content.strip():
                print("Error: file is empty.")
                return None
            return json.loads(content)
    except FileNotFoundError:
        print("Error: File not found.")
        return None
    except json.JSONDecodeError:
        print("Error: file is not valid JSON.")
        return None

def read_file(file_path):
    """
    Reads the content of a file from the specified file path.

    Args:
        file_path (str): The path to the file to be read.

    Returns:
        str: The content of the file as a string.

    Raises:
        FileNotFoundError: If the file does not exist.
        IOError: If there is an error reading the file.
    """
    base_dir = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(base_dir, file_path)

    with open(path, "r") as f:
        return f.read()