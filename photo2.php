<?php
// photo2.php - Simple image description in Latvian
// Returns JSON with single 'text' field

// OpenAI API Configuration, get key from https://platform.openai.com/api-keys
define('OPENAI_API_KEY', 'sk-proj-WP......CVFwA');

// Function to detect and describe image in Latvian
function describeImage($imagePath) {
    if (!file_exists($imagePath)) {
        return ["error" => "Image file not found"];
    }
    
    // Encode image to base64
    $imageData = base64_encode(file_get_contents($imagePath));
    
    // Prepare the API request
    $data = [
        'model' => 'gpt-4o',
        'messages' => [
            [
                'role' => 'user',
                'content' => [
                    [
                        'type' => 'text',
                        'text' => 'Apraksti, kas redzams attēla latviesu valoda. Maksimali 14 vardus.'
                    ],
                    [
                        'type' => 'image_url',
                        'image_url' => [
                            'url' => 'data:image/jpeg;base64,' . $imageData
                        ]
                    ]
                ]
            ]
        ],
        'max_tokens' => 500
    ];
    
    // Make API request
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, 'https://api.openai.com/v1/chat/completions');
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_POST, true);
    curl_setopt($ch, CURLOPT_POSTFIELDS, json_encode($data));
    curl_setopt($ch, CURLOPT_HTTPHEADER, [
        'Content-Type: application/json',
        'Authorization: Bearer ' . OPENAI_API_KEY
    ]);
    curl_setopt($ch, CURLOPT_TIMEOUT, 30);
    curl_setopt($ch, CURLOPT_SSL_VERIFYPEER, true);
    
    $response = curl_exec($ch);
    $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $curlError = curl_error($ch);
    curl_close($ch);
    
    if ($curlError) {
        return ["error" => "cURL error: " . $curlError];
    }
    
    if ($httpCode !== 200) {
        return ["error" => "API request failed (HTTP $httpCode)"];
    }
    
    $result = json_decode($response, true);
    if (isset($result['choices'][0]['message']['content'])) {
        $text = trim($result['choices'][0]['message']['content']);
        
        // Replace Latvian diacritical marks with basic Latin letters
        $text = str_replace(['ā', 'ē', 'ī', 'ū', 'č', 'ģ', 'ķ', 'ļ', 'ņ', 'š', 'ž'], 
                           ['a', 'e', 'i', 'u', 'c', 'g', 'k', 'l', 'n', 's', 'z'], $text);
        
        return ["text" => $text];
    } else {
        return ["error" => "Could not parse API response"];
    }
}

// Set headers
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: POST');
header('Access-Control-Allow-Headers: Content-Type');

// Check if it's a POST request
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    echo json_encode(['error' => 'Method not allowed']);
    exit;
}

// Get the raw POST data
$imageData = file_get_contents('php://input');

if (empty($imageData)) {
    http_response_code(400);
    echo json_encode(['error' => 'No image data received']);
    exit;
}

// Save the image as temp file
$tempFilename = dirname(__FILE__) . '/temp_photo2_' . time() . '.jpeg';
$result = file_put_contents($tempFilename, $imageData);

if ($result === false) {
    http_response_code(500);
    echo json_encode(['error' => 'Failed to save image']);
    exit;
}

// Describe the image
$description = describeImage($tempFilename);

// Clean up temp file
unlink($tempFilename);

// Return JSON response
if (isset($description['error'])) {
    echo json_encode(['error' => $description['error']]);
} else {
    echo json_encode(['text' => $description['text']]);
}
?> 
