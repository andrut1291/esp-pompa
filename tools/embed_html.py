import os

Import("env")

project_dir = env.subst("$PROJECT_DIR")
html_path = os.path.join(project_dir, "data", "index.html")
header_path = os.path.join(project_dir, "lib", "webpage", "embedded_html.h")

with open(html_path, "r") as f:
    html = f.read()

header = '#ifndef EMBEDDED_HTML_H\n#define EMBEDDED_HTML_H\n\nconst char INDEX_HTML[] = R"rawliteral(\n' + html + ')rawliteral";\n\n#endif\n'

with open(header_path, "w") as f:
    f.write(header)
