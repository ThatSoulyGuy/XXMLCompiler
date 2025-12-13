// extension.ts - XXML VS Code Extension Entry Point
// Launches the XXML Language Server and provides IDE features

import * as vscode from 'vscode';
import * as path from 'path';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

// Decoration types for ownership visualization
let ownedDecoration: vscode.TextEditorDecorationType;
let referenceDecoration: vscode.TextEditorDecorationType;
let copyDecoration: vscode.TextEditorDecorationType;

export function activate(context: vscode.ExtensionContext) {
    console.log('XXML extension activating...');

    // Initialize ownership decorations
    initializeOwnershipDecorations(context);

    // Start language server if enabled
    const config = vscode.workspace.getConfiguration('xxml');
    if (config.get('languageServer.enabled', true)) {
        startLanguageServer(context);
    }

    // Register ownership visualization update handler
    if (config.get('ownershipVisualization.enabled', true)) {
        vscode.window.onDidChangeActiveTextEditor(editor => {
            if (editor && editor.document.languageId === 'xxml') {
                updateOwnershipDecorations(editor);
            }
        }, null, context.subscriptions);

        vscode.workspace.onDidChangeTextDocument(event => {
            const editor = vscode.window.activeTextEditor;
            if (editor && event.document === editor.document && editor.document.languageId === 'xxml') {
                updateOwnershipDecorations(editor);
            }
        }, null, context.subscriptions);

        // Update decorations for current editor
        if (vscode.window.activeTextEditor && vscode.window.activeTextEditor.document.languageId === 'xxml') {
            updateOwnershipDecorations(vscode.window.activeTextEditor);
        }
    }

    // Register commands
    context.subscriptions.push(
        vscode.commands.registerCommand('xxml.restartServer', () => {
            restartLanguageServer(context);
        })
    );

    console.log('XXML extension activated');
}

export function deactivate(): Thenable<void> | undefined {
    if (client) {
        return client.stop();
    }
    return undefined;
}

function initializeOwnershipDecorations(context: vscode.ExtensionContext) {
    // Owned (^) - Green color
    ownedDecoration = vscode.window.createTextEditorDecorationType({
        color: '#4CAF50',
        fontWeight: 'bold'
    });

    // Reference (&) - Blue color
    referenceDecoration = vscode.window.createTextEditorDecorationType({
        color: '#2196F3',
        fontWeight: 'bold'
    });

    // Copy (%) - Yellow/Orange color
    copyDecoration = vscode.window.createTextEditorDecorationType({
        color: '#FF9800',
        fontWeight: 'bold'
    });

    context.subscriptions.push(ownedDecoration, referenceDecoration, copyDecoration);
}

function updateOwnershipDecorations(editor: vscode.TextEditor) {
    const document = editor.document;
    const text = document.getText();

    const ownedRanges: vscode.DecorationOptions[] = [];
    const referenceRanges: vscode.DecorationOptions[] = [];
    const copyRanges: vscode.DecorationOptions[] = [];

    // Find ownership modifiers in the text
    // Match patterns like: Type^, Type&, Type%
    const ownershipPattern = /(\w+)([&^%])/g;
    let match;

    while ((match = ownershipPattern.exec(text)) !== null) {
        const modifier = match[2];
        const startPos = document.positionAt(match.index + match[1].length);
        const endPos = document.positionAt(match.index + match[0].length);
        const range = new vscode.Range(startPos, endPos);

        const decoration: vscode.DecorationOptions = {
            range,
            hoverMessage: getOwnershipHoverMessage(modifier)
        };

        switch (modifier) {
            case '^':
                ownedRanges.push(decoration);
                break;
            case '&':
                referenceRanges.push(decoration);
                break;
            case '%':
                copyRanges.push(decoration);
                break;
        }
    }

    editor.setDecorations(ownedDecoration, ownedRanges);
    editor.setDecorations(referenceDecoration, referenceRanges);
    editor.setDecorations(copyDecoration, copyRanges);
}

function getOwnershipHoverMessage(modifier: string): vscode.MarkdownString {
    const messages: Record<string, string> = {
        '^': '**Owned** (`^`)\n\nUnique ownership - this value is responsible for the resource\'s lifetime. When this value goes out of scope, the resource is freed.',
        '&': '**Reference** (`&`)\n\nBorrowed reference - a temporary view into a resource owned elsewhere. Cannot outlive the owner.',
        '%': '**Copy** (`%`)\n\nValue copy - a bitwise copy of the value. Original and copy are independent.'
    };

    const md = new vscode.MarkdownString(messages[modifier] || 'Unknown ownership modifier');
    md.isTrusted = true;
    return md;
}

function startLanguageServer(context: vscode.ExtensionContext) {
    const config = vscode.workspace.getConfiguration('xxml');
    let serverPath = config.get<string>('languageServer.path', '');

    // If no path configured, look for xxml-lsp in common locations
    if (!serverPath) {
        // Check if extension has bundled server
        const bundledPath = context.asAbsolutePath('xxml-lsp.exe');
        if (require('fs').existsSync(bundledPath)) {
            serverPath = bundledPath;
        } else {
            // Fall back to PATH
            serverPath = 'xxml-lsp';
        }
    }

    const serverOptions: ServerOptions = {
        run: {
            command: serverPath,
            transport: TransportKind.stdio
        },
        debug: {
            command: serverPath,
            transport: TransportKind.stdio
        }
    };

    // Get stdlib path and include paths from settings
    const stdlibPath = config.get<string>('stdlibPath', '');
    const includePaths = config.get<string[]>('includePaths', []);

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'xxml' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.{XXML,xxml}'),
            configurationSection: 'xxml'
        },
        outputChannelName: 'XXML Language Server',
        initializationOptions: {
            stdlibPath: stdlibPath,
            includePaths: includePaths
        }
    };

    client = new LanguageClient(
        'xxmlLanguageServer',
        'XXML Language Server',
        serverOptions,
        clientOptions
    );

    // Handle custom ownership decorations notification from server
    client.onNotification('xxml/ownershipDecorations', (params: OwnershipDecorationsParams) => {
        handleOwnershipDecorations(params);
    });

    client.start().then(() => {
        console.log('XXML Language Server started');
    }).catch((error) => {
        console.error('Failed to start XXML Language Server:', error);
        vscode.window.showErrorMessage(
            `Failed to start XXML Language Server: ${error.message}. ` +
            `Make sure xxml-lsp is installed and accessible.`
        );
    });

    context.subscriptions.push(client);
}

function restartLanguageServer(context: vscode.ExtensionContext) {
    if (client) {
        client.stop().then(() => {
            startLanguageServer(context);
        });
    } else {
        startLanguageServer(context);
    }
}

// Custom LSP notification types for ownership visualization
interface OwnershipDecorationsParams {
    uri: string;
    decorations: OwnershipDecoration[];
}

interface OwnershipDecoration {
    range: {
        start: { line: number; character: number };
        end: { line: number; character: number };
    };
    kind: 'owned' | 'reference' | 'copy';
    message?: string;
}

function handleOwnershipDecorations(params: OwnershipDecorationsParams) {
    const editor = vscode.window.visibleTextEditors.find(
        e => e.document.uri.toString() === params.uri
    );

    if (!editor) return;

    const ownedRanges: vscode.DecorationOptions[] = [];
    const referenceRanges: vscode.DecorationOptions[] = [];
    const copyRanges: vscode.DecorationOptions[] = [];

    for (const dec of params.decorations) {
        const range = new vscode.Range(
            dec.range.start.line,
            dec.range.start.character,
            dec.range.end.line,
            dec.range.end.character
        );

        const decoration: vscode.DecorationOptions = {
            range,
            hoverMessage: dec.message ? new vscode.MarkdownString(dec.message) : undefined
        };

        switch (dec.kind) {
            case 'owned':
                ownedRanges.push(decoration);
                break;
            case 'reference':
                referenceRanges.push(decoration);
                break;
            case 'copy':
                copyRanges.push(decoration);
                break;
        }
    }

    editor.setDecorations(ownedDecoration, ownedRanges);
    editor.setDecorations(referenceDecoration, referenceRanges);
    editor.setDecorations(copyDecoration, copyRanges);
}
