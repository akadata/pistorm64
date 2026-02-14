source ~/.profile 
curl -sS -H "Authorization: Bearer $GITHUB_TOKEN" -H "Accept: application/vnd.github+json" "https://api.github.com/repos/akadata/pistorm64/code-scanning/alerts?state=open&per_page=100"
