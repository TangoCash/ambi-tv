import re
import json

class AmbiConfigParser:
    """
    A parser to read and store configurations from a file
    with a structure similar to JSON, supporting comments.
    """
    def __init__(self, file_path):
        self.file_path = file_path
        self.config = {}
        self.parse()

    def parse(self):
        """Parses the configuration file into a dictionary."""
        with open(self.file_path, 'r', encoding='iso-8859-1') as file:
            content = file.read()

        # Remove comments (lines starting with #)
        content = re.sub(r'#.*', '', content)
        
        # Extract all sections and their corresponding content
        pattern = re.compile(r'(\w[\w-]*)\s*{([^}]*)}', re.MULTILINE | re.DOTALL)
        for match in pattern.finditer(content):
            section_name = match.group(1).strip()
            section_content = match.group(2).strip()
            self.config[section_name] = self._parse_section(section_content)

    def _parse_section(self, section_content):
        """Parses a section block into a dictionary."""
        section = {}
        for line in section_content.splitlines():
            line = line.strip()
            if line:  # Skip empty lines
                key_value_match = re.match(r'([\w-]+)\s+(.+)', line)
                if key_value_match:
                    key, value = key_value_match.groups()
                    # Attempt to convert value to int, float, or leave as string
                    value = self._convert_value(value)
                    section[key] = value
        return section

    def _convert_value(self, value):
        """Attempts to convert a string value to an int, float, or leaves it as a string."""
        try:
            if '.' in value and value.replace('.', '', 1).replace('-', '', 1).isdigit():
                return float(value)
            elif value.isdigit():
                return int(value)
            return value.strip()
        except ValueError:
            return value.strip()  # Remove extra spaces

    def get_config(self):
        """Returns the parsed configuration."""
        return self.config

if __name__ == "__main__":
	# Instantiate and parse the configuration file with the updated parser
	parsed_config = AmbiConfigParser('ambi-tv-ledsim.conf').get_config()

	#print(parsed_config)  # Display the corrected parsed configuration.
	print(json.dumps(parsed_config, indent=4, default=str))
