# Confidential Forum sample app

NOTE: This sample is a work-in-progress.

Install dependencies:

```sh
npm install
```

Start the sandbox:

```sh
npm start
```

Open your browser at https://127.0.0.1:8000/app/site

Generate opinions, user identities and submit:

```sh
python3.8 test/demo/generate-opinions.py test/demo/polls.csv 9
npm run ts test/demo/generate-jwts.ts . 9
npm run ts test/demo/submit-opinions.ts .
```

Run tests:

```sh
npm test
```
