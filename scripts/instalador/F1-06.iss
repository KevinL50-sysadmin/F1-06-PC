; Instalador de F1 06 recompilado (Inno Setup 6). No se compila a mano: lo hace scripts\empaquetar.ps1 -Instalador,
; que pasa la carpeta portable ya hecha (Origen), el nombre del .exe, la version, la fecha, el icono (Icono:
; build\icono\F1-06.ico, el logo del juego, de scripts\hacer-icono.ps1) y la carpeta de salida.
; Instala el .exe, las DLL, data\, README.txt (ingles), LEEME.txt (espanol), recomplauncher.json (RecompLauncher) y
; licenses\; sin portable.txt, asi que el
; juego guarda lo del usuario en Documentos\My Games\F1-06 (config, saves, mods, screenshots) y los registros en
; AppData\Local\F1-06\logs.
; v1.1.1 (odd/tasks/player-folders.md): la carpeta del jugador va en ingles. Al actualizar desde la 1.1.0 se quitan
; "Jugar en red - Invitado.cmd" y licencias\ ([InstallDelete]) y {app}\datos se renombra a {app}\data (el juego no
; puede en Archivos de programa); al desinstalar se borran las copias del disco con los dos nombres.
; Los datos pasan de 4 GB: Setup.exe + archivos .bin (DiskSpanning) del tamano maximo.
; Compresion lzma2/fast en varios hilos: la mayor parte de los datos es audio y video ya comprimido (lzma o zip
; solo lo dejan en ~81 %), y lzma2/fast reduce mas los .WAD de graficos sin pasar de unos minutos.
; Lanzamiento T1: con /DSinDatos (empaquetar.ps1 -SinDatos, la version publica) no lleva data\: un solo Setup.exe
; con la pagina "Elige tu copia del juego (ISO)", que al instalar ejecuta {#NombreExe} --extraer-iso <iso> --ventana
; (su barra de progreso, cancelable; en silencio, sin --ventana: fallos-con-log T9). En blanco, el juego pide la ISO
; la primera vez que se abre.

#ifndef Origen
  #error Falta /DOrigen=<carpeta portable>: compila con scripts\empaquetar.ps1 -Instalador
#endif
#ifndef NombreExe
  #define NombreExe "F1-06.exe"
#endif
#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Fecha
  #define Fecha ""
#endif
; Lanzamiento T4: la publica es otra aplicacion para Windows (su AppId, su carpeta, sus accesos y su nombre en
; Aplicaciones): instalarla o desinstalarla nunca toca la privada (1.0.0, con datos), que sigue con su AppId de siempre.
#ifdef SinDatos
  #define IdApp "{{987B3066-7B28-471B-ABAA-4BC0FE05E9FF}"
  #define NombreApp "F1 06 PC"
  #define CarpetaApp "F1-06-PC"
#else
  #define IdApp "{{C7452AD4-DFFE-4E89-ACBC-107B51DCACB7}"
  #define NombreApp "F1 06"
  #define CarpetaApp "F1-06"
#endif

[Setup]
AppId={#IdApp}
AppName={#NombreApp}
AppVersion={#Version} ({#Fecha})
AppVerName={#NombreApp} {#Version}
VersionInfoVersion={#Version}
VersionInfoDescription=Instalador de {#NombreApp}
DefaultDirName={autopf}\{#CarpetaApp}
DisableProgramGroupPage=yes
; Por defecto pide administrador (Archivos de programa); el dialogo deja instalar solo para el usuario actual
; sin permisos (y /CURRENTUSER en la linea de ordenes).
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=F1-06-Setup
#ifdef SinDatos
Compression=lzma2/ultra64
SolidCompression=yes
; Lo que ocupara data\ al copiar la ISO (Inno avisa si no cabe).
ExtraDiskSpaceRequired=4500000000
#else
Compression=lzma2/fast
SolidCompression=no
LZMANumBlockThreads=8
LZMAUseSeparateProcess=yes
DiskSpanning=yes
DiskSliceSize=max
#endif
; El .exe lleva el icono dentro: es el que sale en Configuracion > Aplicaciones.
UninstallDisplayIcon={app}\{#NombreExe}
#ifdef Icono
SetupIconFile={#Icono}
#endif
UninstallDisplayName={#NombreApp}
WizardStyle=modern
; Imagenes del asistente (scripts\hacer-imagen-instalador.ps1: la portada retocada como caja de PC), una por escala.
#ifdef ImagenAsistente
WizardImageFile={#ImagenAsistente}
; La pagina de bienvenida (Inno Setup 6 la quita por defecto) es la que ensena la imagen al entrar.
DisableWelcomePage=no
#endif
#ifdef ImagenPequena
WizardSmallImageFile={#ImagenPequena}
#endif
ShowLanguageDialog=no

[Languages]
; El idioma sale del de Windows (sin preguntar): espanol, o ingles en cualquier otro.
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
es.BorrarPartidas=¿Borrar también tus partidas y opciones (Documentos\My Games\F1-06)?
en.BorrarPartidas=Also delete your saves and options (Documents\My Games\F1-06)?
es.InvitadoComentario=Conectarse como jugador 2 al PC del anfitrion
en.InvitadoComentario=Connect as player 2 to the host's PC
es.IsoTitulo=Elige tu copia del juego (ISO)
en.IsoTitulo=Choose your copy of the game (ISO)
es.IsoDescripcion=El juego no trae nada del disco: hace falta tu Formula One 06 de PS2 (europeo PAL, SCES-53950).
en.IsoDescripcion=The game includes nothing from the disc: your Formula One 06 for PS2 (European PAL, SCES-53950) is needed.
es.IsoTexto=Elige la imagen .iso de tu disco. Al instalar se comprueba y sus archivos se copian en la carpeta data del juego (unos 4,2 GB, unos minutos, con una barra de progreso); la ISO solo se lee.%n%nSi lo dejas en blanco, el juego te la pedirá la primera vez que lo abras.
en.IsoTexto=Choose the .iso image of your disc. During the installation it is checked and its files are copied to the game's data folder (about 4.2 GB, a few minutes, with a progress bar); the ISO is only read.%n%nIf you leave it blank, the game asks for it the first time you open it.
es.IsoCampo=Imagen del disco (.iso):
en.IsoCampo=Disc image (.iso):
es.IsoFiltro=Imagen de disco de PS2 (*.iso)|*.iso|Todos los archivos|*.*
en.IsoFiltro=PS2 disc image (*.iso)|*.iso|All files|*.*
es.IsoNoExiste=Ese archivo no existe. Elige tu .iso o deja el campo en blanco.
en.IsoNoExiste=That file does not exist. Choose your .iso or leave the field blank.
es.IsoCopiando=Copiando los archivos de tu disco...
en.IsoCopiando=Copying the files of your disc...
es.IsoSinCopiar=No se han copiado los archivos del disco. El juego te pedirá tu copia la primera vez que lo abras.
en.IsoSinCopiar=The disc files were not copied. The game will ask for your copy the first time you open it.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#Origen}\{#NombreExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Origen}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Origen}\README.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Origen}\LEEME.txt"; DestDir: "{app}"; Flags: ignoreversion
; RecompLauncher (odd/tasks/recomplauncher-json.md): junto al .exe; sin portable.txt usa las rutas de Documentos y AppData
Source: "{#Origen}\recomplauncher.json"; DestDir: "{app}"; Flags: ignoreversion
; Licencias de paraLLEl-GS (LGPL) y sus dependencias, con su codigo fuente (lo genera empaquetar.ps1)
Source: "{#Origen}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
#ifndef SinDatos
Source: "{#Origen}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs
#endif

[InstallDelete]
; player-folders T3: nombres de la 1.1.0 que pone el paquete (no son del jugador); se rehacen con los nuevos.
Type: files; Name: "{app}\Jugar en red - Invitado.cmd"
Type: filesandordirs; Name: "{app}\licencias"

[Icons]
Name: "{autoprograms}\{#NombreApp}"; Filename: "{app}\{#NombreExe}"; WorkingDir: "{app}"
; Fase 5 (T15, N4): el invitado del juego en red (su formulario: direccion, puerto y contrasena del anfitrion).
Name: "{autoprograms}\{#NombreApp} - Jugar en red (invitado)"; Filename: "{app}\{#NombreExe}"; Parameters: "--invitado"; WorkingDir: "{app}"; Comment: "{cm:InvitadoComentario}"
Name: "{autodesktop}\{#NombreApp}"; Filename: "{app}\{#NombreExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#NombreExe}"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,{#NombreApp}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; La carpeta del programa se borra siempre (con lo que el juego haya dejado en ella).
Type: filesandordirs; Name: "{app}"

[Code]
// player-folders T3: {app}\datos de la 1.1.0 (la privada lo instalaba; la publica lo copiaba de la ISO) pasa a
// {app}\data antes de copiar los archivos, para no dejar dos copias del disco (unos 4 GB). Solo si no hay data: si
// estan los dos, se dejan como estan (el juego usa data). Si no se puede renombrar, sigue datos y el juego lo lee.
procedure RenombrarDatosViejos;
var
  Viejo, Nuevo: String;
begin
  Viejo := ExpandConstant('{app}\datos');
  Nuevo := ExpandConstant('{app}\data');
  if DirExists(Viejo) and not DirExists(Nuevo) and not FileExists(Nuevo) then
  begin
    if RenameFile(Viejo, Nuevo) then
      Log('Renombrada ' + Viejo + ' a ' + Nuevo)
    else
      Log('No se pudo renombrar ' + Viejo + ' a ' + Nuevo + ': se deja');
  end;
end;

#ifdef SinDatos
var
  PaginaIso: TInputFileWizardPage;

function ContieneTexto(const Texto, Parte: String): Boolean;
begin
  Result := Pos(Lowercase(Parte), Lowercase(Texto)) > 0;
end;

// La primera .iso de la carpeta (sin subcarpetas) con "formula" o "f1" en el nombre, o ''. El .exe la comprueba
// de verdad al copiarla.
function BuscarIso(const Carpeta: String): String;
var
  Busqueda: TFindRec;
begin
  Result := '';
  if (Carpeta = '') or not DirExists(Carpeta) then
    Exit;
  if FindFirst(AddBackslash(Carpeta) + '*.iso', Busqueda) then
  try
    repeat
      if ((Busqueda.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0) and
          (ContieneTexto(Busqueda.Name, 'formula') or ContieneTexto(Busqueda.Name, 'f1')) then
      begin
        Result := AddBackslash(Carpeta) + Busqueda.Name;
        Exit;
      end;
    until not FindNext(Busqueda);
  finally
    FindClose(Busqueda);
  end;
end;

// Descargas puede estar en otro disco: Shell Folders tiene la ruta ya expandida.
function CarpetaDescargas: String;
begin
  if not RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Folders',
      '{374DE290-123F-4565-9164-39C4925E467B}', Result) then
    Result := ExpandConstant('{%USERPROFILE}\Downloads');
end;

procedure InitializeWizard;
var
  Encontrada: String;
begin
  PaginaIso := CreateInputFilePage(wpSelectTasks, CustomMessage('IsoTitulo'), CustomMessage('IsoDescripcion'),
    CustomMessage('IsoTexto'));
  PaginaIso.Add(CustomMessage('IsoCampo'), CustomMessage('IsoFiltro'), '.iso');
  Encontrada := BuscarIso(CarpetaDescargas);
  if Encontrada = '' then
    Encontrada := BuscarIso(ExpandConstant('{userdesktop}'));
  PaginaIso.Values[0] := Encontrada;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = PaginaIso.ID) and (Trim(PaginaIso.Values[0]) <> '') and
      not FileExists(Trim(PaginaIso.Values[0])) then
  begin
    SuppressibleMsgBox(CustomMessage('IsoNoExiste'), mbError, MB_OK, IDOK);
    Result := False;
  end;
end;

// fallos-con-log T9 (L31): las lineas de un flujo del .exe en el registro de /LOG, como mucho 60. Las "progress
// <0-1> <texto>" (para RecompLauncher, una por centesima) no van: solo la ultima.
procedure RegistrarSalida(const Flujo: String; const Lineas: TArrayOfString);
var
  I, Escritas: Integer;
  UltimoProgreso: String;
begin
  Escritas := 0;
  UltimoProgreso := '';
  for I := 0 to GetArrayLength(Lineas) - 1 do
    if Copy(Lineas[I], 1, 9) = 'progress ' then
      UltimoProgreso := Lineas[I]
    else if Escritas < 60 then
    begin
      Log(Flujo + ': ' + Lineas[I]);
      Escritas := Escritas + 1;
    end;
  if UltimoProgreso <> '' then
    Log(Flujo + ' (ultima progress): ' + UltimoProgreso);
  if Escritas = 60 then
    Log(Flujo + ': ... (se cortan las demas lineas)');
end;

// Tras copiar los archivos: el .exe comprueba la ISO y la copia en {app}\data con su ventana de progreso (y sus
// mensajes de error). Si no se copia, el juego la pide al abrirse.
// fallos-con-log T9 (L31): en silencio (/SILENT, /VERYSILENT) va sin --ventana: una ventana del .exe (error o
// progreso, salen visibles aunque se oculte el proceso) dejaba la instalacion esperando sin limite y sin rastro.
// En los dos casos la orden, el codigo de salida y las lineas [disco] del .exe quedan en el registro de /LOG.
procedure CopiarIso;
var
  Iso, Parametros: String;
  Codigo: Integer;
  Salida: TExecOutput;
  Hecho: Boolean;
begin
  Iso := Trim(PaginaIso.Values[0]);
  if Iso = '' then
    Exit;
  WizardForm.StatusLabel.Caption := CustomMessage('IsoCopiando');
  Parametros := '--extraer-iso "' + Iso + '"';
  if not WizardSilent then
    Parametros := Parametros + ' --ventana';
  Log('Copia de la ISO: ' + ExpandConstant('{app}\{#NombreExe}') + ' ' + Parametros);
  try
    Hecho := ExecAndCaptureOutput(ExpandConstant('{app}\{#NombreExe}'), Parametros, ExpandConstant('{app}'),
      SW_SHOWNORMAL, ewWaitUntilTerminated, Codigo, Salida);
  except
    Hecho := False;
    Codigo := -1;
    Log('Copia de la ISO: ' + GetExceptionMessage);
  end;
  if Hecho then
  begin
    RegistrarSalida('salida', Salida.StdOut);
    RegistrarSalida('errores', Salida.StdErr);
    Log('Copia de la ISO: codigo de salida ' + IntToStr(Codigo));
  end
  else if Codigo <> -1 then
    Log('Copia de la ISO: no se pudo ejecutar (' + SysErrorMessage(Codigo) + ')');
  if not Hecho or (Codigo <> 0) then
    SuppressibleMsgBox(CustomMessage('IsoSinCopiar'), mbInformation, MB_OK, IDOK);
end;
#endif

procedure CurStepChanged(CurStep: TSetupStep);
begin
  // ssInstall: antes de [InstallDelete] y de copiar los archivos.
  if CurStep = ssInstall then
    RenombrarDatosViejos;
#ifdef SinDatos
  if CurStep = ssPostInstall then
    CopiarIso;
#endif
end;

#ifdef SinDatos
// Una copia del disco con el nombre Nombre: la carpeta, su copia a medias (Nombre.extrayendo) y las incompletas que
// aparto el juego (Nombre.anterior, Nombre.anterior-2...).
procedure BorrarCopiaDelDisco(const Carpeta, Nombre: String);
var
  Busqueda: TFindRec;
begin
  DelTree(Carpeta + '\' + Nombre, True, True, True);
  DelTree(Carpeta + '\' + Nombre + '.extrayendo', True, True, True);
  if FindFirst(Carpeta + '\' + Nombre + '.anterior*', Busqueda) then
  try
    repeat
      if ((Busqueda.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0) and
          ((CompareText(Busqueda.Name, Nombre + '.anterior') = 0) or
           (CompareText(Copy(Busqueda.Name, 1, Length(Nombre) + 10), Nombre + '.anterior-') = 0)) then
        DelTree(Carpeta + '\' + Busqueda.Name, True, True, True);
    until not FindNext(Busqueda);
  finally
    FindClose(Busqueda);
  end;
end;

// Lanzamiento T1: la copia del disco que el juego hizo en LocalAppData (instalado en Archivos de programa sin
// elegir la ISO al instalar) se borra siempre, como la de {app}\data: se rehace con la ISO. Con el nombre de la 1.1.1
// (data) y el de la 1.1.0 (datos) si el juego no llego a renombrarla (player-folders T3). Solo en la publica: la
// privada lleva data\ en {app} y nunca escribe ahi.
procedure BorrarCopiasDelDisco(const Carpeta: String);
begin
  BorrarCopiaDelDisco(Carpeta, 'data');
  BorrarCopiaDelDisco(Carpeta, 'datos');
end;
#endif

// Al desinstalar: las partidas y opciones del usuario solo se borran si dice que si (por defecto, y en modo
// silencioso, no).
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Usuario, Registros: String;
begin
  if CurUninstallStep <> usUninstall then
    Exit;
  Usuario := ExpandConstant('{userdocs}\My Games\F1-06');
  Registros := ExpandConstant('{localappdata}\F1-06');
#ifdef SinDatos
  BorrarCopiasDelDisco(Registros);
#endif
  // Lanzamiento T4: /SILENT no calla SuppressibleMsgBox (solo /SUPPRESSMSGBOXES): sin esto la pregunta paraba la
  // desinstalacion silenciosa.
  if UninstallSilent or (not DirExists(Usuario) and not DirExists(Registros)) then
    Exit;
  if SuppressibleMsgBox(CustomMessage('BorrarPartidas'),
      mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES then
  begin
    DelTree(Usuario, True, True, True);
    DelTree(Registros, True, True, True);
  end;
end;
