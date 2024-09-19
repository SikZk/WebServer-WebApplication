import * as React from 'react';
import TextField from '@mui/material/TextField';
import Button from '@mui/material/Button';

export default function App() {
    const [dataSentToServer, setDataSentToServer] = React.useState('');
    const [dataReceivedFromServer, setDataReceivedFromServer] = React.useState('');
    const [errorData, setErrorData] = React.useState('');

    const [username, setUsername] = React.useState('');
    const [password, setPassword] = React.useState('');


  return (
    <>
      <div className="header">
          <h1>Communication monitor</h1>
          <h4>HTTP communication between this Frontend App hosted on Custom webserver, and Backend App also made in C from scratch.</h4>
      </div>
      <div className="contentContainer">
          <div className="HttpBlock">
              <div>
                  <div style={{
                      textAlign: 'center',
                      marginBottom: '5px'
                  }}>
                      Login form to test backend endpoint /api/v1/login<br />
                      correct credentials - <strong>user1:password1</strong>
                  </div>
                  <TextField id="filled-basic" label="email" variant="filled" value={username} onChange={(event) => {setUsername(event.target.value);}} sx={{
                      marginRight: '10px'
                  }}/>
                  <TextField id="filled-basic" label="password" variant="filled" value={password} onChange={(event) => {setPassword(event.target.value);}}/>
                  <div style={{
                      textAlign: 'center',
                      marginTop: '5px'
                  }}>
                      <Button variant="contained" sx={{backgroundColor: '#A04747;'}}
                      onClick={() =>{
                          fetch('http://localhost:1111/api/v1/login', {
                              method: 'POST',
                              headers: {
                                  'Content-Type': 'application/json'
                              },
                              body: JSON.stringify({ email: username, password: password })
                          })
                              .then(async response => {
                                  const responseText = await response.text();
                                  let responseDetails;
                                  if(response.status === 200){
                                      responseDetails = `
                                        HTTP/1.0 ${response.status} ${response.statusText}
                                        Server: CS241Serv v0.1
                                        Content-Type: text/html
                                        Set-Cookie: session=1
                                        Access-Control-Allow-Origin: *
                                        
                                        ${responseText}
                                      `;
                                  }else if(response.status === 400){
                                      responseDetails = `
                                        HTTP/1.0 ${response.status} ${response.statusText}
                                        Server: CS241Serv v0.1
                                        Access-Control-Allow-Origin: *
                                        Content-Type: text/html
                                        
                                        ${responseText}
                                      `;
                                  }else if(response.status === 404){
                                      responseDetails = `
                                        HTTP/1.0 ${response.status} ${response.statusText}
                                        Server: CS241Serv v0.1
                                        Access-Control-Allow-Origin: *
                                        Content-Type: text/html
                                        
                                        ${responseText}
                                      `;
                                  }else{
                                      responseDetails = ``;
                                  }

                                  const requestDetails = `
                                    POST /api/v1/login HTTP/1.1
                                    Host: localhost:1111
                                    Content-Type: application/json
                                    User-Agent: FrontendApplication
                                    
                                    ${JSON.stringify({ email: username, password: password }, null, 2)}
                                `;

                                  setDataSentToServer(requestDetails);
                                  setDataReceivedFromServer(responseDetails);
                                  setErrorData('');
                              })
                              .catch(error => {
                                  setErrorData(error.toString());
                              });
                      }}
                      >Send Data</Button>
                  </div>
                  <div style={{textAlign: 'center', marginTop: '20px'}}>Error data</div>
                  <div style={{
                      height: '90%',
                      border: '2px solid black',
                      borderRadius: '5px',
                      marginTop: '5px',
                      padding: '5px'
                  }}>
                      {errorData}
                  </div>
              </div>
          </div>
          <div className="HttpBlock">
              <div style={{
                  height: '50%',
                  width: '90%',
                  alignItems: 'center',
                  textAlign: 'center'
              }}>
                  <div>Data sent to server</div>
                  <div style={{
                      height: '85%',
                      border: '2px solid black',
                      borderRadius: '5px',
                      marginTop: '5px',
                      padding: '5px',
                      whiteSpace: 'pre-wrap',
                      textAlign: 'left',
                      fontFamily: 'monospace'
                  }}>
                      <pre>
                        <code>
                          {dataSentToServer
                              .split('\n')
                              .map(line => line.trim())
                              .join('\n')}
                        </code>
                      </pre>
                  </div>
              </div>
              <div style={{
                  height: '50%',
                  width: '90%',
                  alignItems: 'center',
                  textAlign: 'center'
              }}>
                  <div>Data received from server</div>
                  <div style={{
                      height: '85%',
                      border: '2px solid black',
                      borderRadius: '5px',
                      marginTop: '5px',
                      padding: '5px',
                      whiteSpace: 'pre-wrap',
                      textAlign: 'left',
                      fontFamily: 'monospace'
                  }}>
                      <pre>
                        <code>
                          {dataReceivedFromServer
                              .split('\n')
                              .map(line => line.trim())
                              .join('\n')}
                        </code>
                      </pre>
                  </div>
              </div>
          </div>
      </div>
    </>
  );
}

